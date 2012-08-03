/*
 * Load of Fun Door Controller daemon process
 * Copyright 2012 Nathaniel Wesley Filardo <nwf@ietfng.org>
 *
 * Released under AGPLv3; see COPYING for details.
 */

// Configuration Knobs (i.e. symbols for mysterious constants)          {{{
#define REMOTE_WRITER_TIMEOUT 30
#define REMOTE_STRING_MAXLEN 128

/*
 * A knob to limit the size of our buffer for reading from the RFID device.
 * Should be set to be large enough to contain any valid response, including
 * newline termination.
 */
#define DOOR_STRING_MAXLEN 12

#define LOCK_DEFAULT_TIME  10

#undef  LOG_TO_SYSLOG
#define LOG_TO_STDERR


#define DEBUG
#undef DEBUG_QUIT_ON_QUIT

#define DOOR_TERMIOS_CFLAGS B2400
/* Some pins may be on the attached serial line */
#undef DOOR_USE_SERIAL_PINS
// #define DOOR_SER_PIN_LOCK   TIOCM_DTR
// #define DOOR_SER_PIN_ONAIR  TIOCM_RTS
// #define DOOR_SER_PIN_OPENED TIOCM_CTS

//                                                                      }}}
// Prelude                                                              {{{
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>

#include <sqlite3.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/util.h>
#include <openssl/evp.h>

#include "controller/common.h"

#define ASIZE(n) (sizeof(n)/sizeof(n[0]))

//                                                                      }}}
// Logging                                                              {{{

/* This logging module provides a "ring-buffer" of sorts by trimming from
 * the beginning whenever we go over size.  We use syslog as well, but this
 * we can use to dump logs over a remote socket.  (Though note that we use a
 * timeout to ensure that slow-loris-style attacks don't eat up our memory.)
 */

#define TRIM_STEP 32 
static void eb_trim_cb(struct evbuffer *b,
                       const struct evbuffer_cb_info *i,
                       void *_ts) {
  if(i->n_added == 0) return;

  int len = evbuffer_get_length(b);
  if (len > ((size_t)_ts)) {
    // Try to read some lines to drop, before whacking off an arbitrary
    // chunk of buffer.
    int len2;
    do {
      free(evbuffer_readln(b, NULL, EVBUFFER_EOL_CRLF));
      len2 = evbuffer_get_length(b);
    } while ((len2 > ((size_t)_ts)) && (len != len2));

    if (len2 > ((size_t)_ts)) {
      int firstchunk = evbuffer_get_contiguous_space(b);
      evbuffer_drain(b, firstchunk < TRIM_STEP ? TRIM_STEP : firstchunk);
    }
  }
}

/* The one program-wide ring buffer */
static struct evbuffer *log_eb;
static struct evbuffer_cb_entry *log_eb_cbe;

static void log_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  evbuffer_add_vprintf(log_eb, fmt, ap);
  va_end(ap);
#ifdef LOG_TO_STDERR
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
#endif
#ifdef LOG_TO_SYSLOG
  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
#endif
}


#define LOG_TARGET_SIZE 1024
static void log_init() {
  log_eb = evbuffer_new();
  assert(log_eb);

  log_eb_cbe = evbuffer_add_cb(log_eb, eb_trim_cb, (void *)LOG_TARGET_SIZE);
  assert(log_eb_cbe);

  openlog("doorcontrol", LOG_PID, LOG_DAEMON);
}


//                                                                      }}}
// Database                                                             {{{

sqlite3 *db;

static void db_pwhash(sqlite3_context *context, int argc, sqlite3_value **argv){
  int res;

  assert( argc==2 );
  assert( sqlite3_value_type(argv[0]) == SQLITE_BLOB );
  assert( sqlite3_value_type(argv[1]) == SQLITE3_TEXT );

  const int saltsize = sqlite3_value_bytes(argv[0]);
  const unsigned char *salt = sqlite3_value_blob(argv[0]);

  const int pwsize = sqlite3_value_bytes(argv[1]);
  const unsigned char *pw = sqlite3_value_blob(argv[1]);

  unsigned char *out = calloc(sizeof (unsigned char), DB_PWHASH_OUTLEN);

  res = PKCS5_PBKDF2_HMAC_SHA1((const char *)pw, pwsize, salt, saltsize,
                               DB_PWHASH_ITERS, DB_PWHASH_OUTLEN, out);
  assert(res != 0);

  sqlite3_result_blob(context, out, DB_PWHASH_OUTLEN, free);
}

sqlite3_stmt *db_gate_rfid_stmt;
sqlite3_stmt *db_gate_password_stmt;

static void db_deinit() {
  sqlite3_finalize(db_gate_password_stmt);
  sqlite3_finalize(db_gate_rfid_stmt);
  sqlite3_close(db);
}

static int db_init(char *fn) {
  int ret;
  char *err;
  const char *tail;
  static const char *db_init_sql = DATABASE_SCHEMA_CREATE;

  ret = sqlite3_open(fn, &db);
  if (ret) {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
    goto err;
  }

  ret = sqlite3_create_function_v2(db, "pwhash", 2, SQLITE_UTF8, NULL,
                                   db_pwhash, NULL, NULL, NULL);
  assert(ret == 0);

  ret = sqlite3_exec(db, db_init_sql, NULL, NULL, &err);
  if (ret != SQLITE_OK) {
    fprintf(stderr, "Can't prepare database: %s (%s)\n", sqlite3_errmsg(db), err);
    if(err) { sqlite3_free(err); }
    goto err;
  }

#define DB_GATE_PARAM_SECRET_IX 1
#define DB_GATE_PARAM_EMAIL_IX 2
#define DB_GATE_RESULT_IX 0
static const char db_gate_password_sql[] =
    "SELECT email FROM users"
    " WHERE email = ?2 "
    "  AND pw = pwhash(users.pwsalt,?1)"
    "  AND enabled = 1;";
  ret = sqlite3_prepare_v2(db, db_gate_password_sql, ASIZE(db_gate_password_sql),
                               &db_gate_password_stmt, &tail);
   if (ret != SQLITE_OK || *tail != '\0') {
    fprintf(stderr, "Can't prepare database: %s (%s)\n", sqlite3_errmsg(db), tail);
    goto err;
  }

static const char db_gate_rfid_sql[] =
    "SELECT email FROM users JOIN rfid USING (user_id) WHERE token = ?1;";
  ret = sqlite3_prepare_v2(db, db_gate_rfid_sql, ASIZE(db_gate_rfid_sql),
                               &db_gate_rfid_stmt, &tail);
   if (ret != SQLITE_OK || *tail != '\0') {
    fprintf(stderr, "Can't prepare database: %s (%s)\n", sqlite3_errmsg(db), tail);
    goto err;
  }


  return 0;
err:
  db_deinit();
  return -1;
}

static int _db_gated_call(sqlite3_stmt *st, int (*c)(const unsigned char *, void *), void *arg) {
  int ret, res = -1;

  ret = sqlite3_step(st);
  if(ret == SQLITE_ROW) {
    res = c(sqlite3_column_text(st, DB_GATE_RESULT_IX), arg);
    ret = sqlite3_step(st);
    /* Guaranteed by UNIQUE constraint */
    assert(ret != SQLITE_ROW);
  }
  ret = sqlite3_clear_bindings(st);
  assert(ret == SQLITE_OK);
  ret = sqlite3_reset(st);
  assert(ret == SQLITE_OK);

  return res;
}

static int db_gate_password(char *e, char *p, int (*c)(const unsigned char *, void *), void *arg) {
  int ret;
  sqlite3_stmt *st = db_gate_password_stmt;

  ret = sqlite3_bind_text(st, DB_GATE_PARAM_EMAIL_IX, e, -1, SQLITE_STATIC); 
  assert(ret == SQLITE_OK);
  ret = sqlite3_bind_text(st, DB_GATE_PARAM_SECRET_IX, p, -1, SQLITE_STATIC); 
  assert(ret == SQLITE_OK);

  return _db_gated_call(st, c, arg);
}

static int db_gate_rfid(char *t, int (*c)(const unsigned char *, void *), void *arg) {
  int ret;
  sqlite3_stmt *st = db_gate_rfid_stmt;

  ret = sqlite3_bind_text(st, DB_GATE_PARAM_SECRET_IX, t, -1, SQLITE_STATIC); 
  assert(ret == SQLITE_OK);

  return _db_gated_call(st, c, arg);
}


//                                                                      }}}
// Serial Utilities                                                     {{{
#ifdef DOOR_USE_SERIAL_PINS
static int serial_pin_fd;

static void serial_mbis(int flags) {
  int res = ioctl(lock_ser_fd, TIOCMBIS, &flags);
  assert(res -= 0);
}

static void serial_mbic(int flags) {
  int res = ioctl(serial_pin_fd, TIOCMBIC, &flags);
  assert(res == 0);
}

static void serial_pin_lock_init() {
  int res;

  /* Tell the OS to lower modem control signals on exit,
   * which will re-engage the lock if we crash.
   */
  struct termios tios;
  memset(&tios, 0, sizeof(tios));
  res = tcgetattr(serial_pin_fd, &tios);
  assert(res == 0);
  tios.c_cflag &= ~(CRTSCTS);
  tios.c_cflag |= HUPCL | CLOCAL;
  res = tcsetattr(serial_pin_fd, TCSADRAIN, &tios);
  assert(res == 0);

  /* De-assert DTR to engage the lock */
  serial_mbic(DOOR_SER_PIN_LOCK);
}
#endif

static void serial_deinit(int fd) {
  /* 
   * Try to set sane tty flags, notably including B0
   */
  tcflush(fd, TCIOFLUSH);

  struct termios tios;
  memset(&tios, 0, sizeof(tios));
  tcgetattr(fd, &tios);
  tios.c_cflag &= ~(CBAUD|CBAUDEX|CRTSCTS);
  tios.c_cflag |= HUPCL | CLOCAL | B0;
  tcsetattr(fd, TCSAFLUSH, &tios);

  close(fd);
}
//                                                                      }}}
// GPIO utilities                                                       {{{

static void gpio_write(FILE *f, int fl) {
  if(f) {
    int res = fprintf(f, fl ? "1\n" : "0\n");
    assert(res == 2);
    res = fflush(f);
    assert(res == 0);
  } 
}

//                                                                      }}}
// Lock control                                                         {{{
FILE* lock_gpio_file;
static struct event   lock_timeout_ev;
static struct timeval lock_timeout_tv;
static void lock_timeout_cb(evutil_socket_t fd, short what, void *arg) {
  log_printf("Lock timeout\n");
#ifdef DOOR_SER_PIN_LOCK
  serial_mbic(DOOR_SER_PIN_LOCK);
#endif
  gpio_write(lock_gpio_file, 1);
}

static int lock_open(const unsigned char *n, void *_ign) {
#ifdef DOOR_SER_PIN_LOCK
  serial_mbis(DOOR_SER_PIN_LOCK);
#endif

  gpio_write(lock_gpio_file, 0);

  if (event_pending(&lock_timeout_ev, EV_TIMEOUT, NULL)) {
    log_printf("Lock re-opened: '%s'\n", n);
  } else {
    log_printf("Lock open: '%s'\n", n);
  }
  event_add(&lock_timeout_ev, &lock_timeout_tv);

  return 0;
}

static int lock_time = LOCK_DEFAULT_TIME;
static void lock_init(struct event_base *base) {
#ifdef DOOR_SER_PIN_LOCK
    serial_pins_lock_init();
#endif

  evtimer_assign(&lock_timeout_ev, base, lock_timeout_cb, NULL);
  timerclear(&lock_timeout_tv);
  lock_timeout_tv.tv_sec = lock_time;
}


//                                                                      }}}
// RFID Control                                                         {{{

static int rfid_fd;
static struct event     rfid_ev;
static struct evbuffer *rfid_eb;

static int rfid_flags;
#define RFID_FLAG_RECOVERING 1

static void rfid_rx_cb(evutil_socket_t fd, short what, void *arg) {
  int ret = 0;
  char *line = NULL;

  ret = evbuffer_read(rfid_eb, fd, 2*DOOR_STRING_MAXLEN);
  assert(ret > 0);

  do {
    line = evbuffer_readln(rfid_eb, NULL, EVBUFFER_EOL_ANY);
    // printf("doorR %d %s\n", rfid_flags, line);
 
    if(line) { 
      if(rfid_flags & RFID_FLAG_RECOVERING) {
        // printf("doorE recovered (discarding %s)\n", line);
      } else {
        // printf("doorF %s (%zd)\n", line, strlen(line));
        if(db_gate_rfid(line, lock_open, NULL)) {
          log_printf("Unknown RFID tag: %s\n", line);
        }
      }
      rfid_flags &= ~RFID_FLAG_RECOVERING;
      free(line);
    }
  } while (line != NULL);

  int len = evbuffer_get_length(rfid_eb);
  if(len >= DOOR_STRING_MAXLEN) {
    // printf("Drop %d\n", len);
    rfid_flags |= RFID_FLAG_RECOVERING;
    evbuffer_drain(rfid_eb, len);
  }
}

static struct event   door_probe_ev;
static struct timeval door_probe_tv;

static void door_probe_status(evutil_socket_t _fd, short what, void *arg) {
  static int vlast = 0; 

  int vnow = 0;
#ifdef DOOR_SER_PIN_OPENED
  {
    int flags = 0;
    int res = ioctl(rfid_fd, TIOCMGET, &flags);
    assert(res == 0);
  }
  vnow = !!(flags & DOOR_SER_PIN_OPENED);
#endif
  if(vnow != vlast) {
    vlast = vnow;
    log_printf("DOOR PROBE CHANGE %d\n", vnow);
    // XXX Do something about it!
  }
}

static void rfid_init(struct event_base *base, int fd) {
  rfid_fd = fd;

  {
  // Set baud rate
  struct termios tios;
  memset(&tios, 0, sizeof(tios));
  int res = tcgetattr(fd, &tios);
  assert(res == 0);
  tios.c_cflag &= ~(CBAUD|CBAUDEX);
  tios.c_cflag |= CLOCAL | DOOR_TERMIOS_CFLAGS;
  res = tcsetattr(fd, TCSADRAIN, &tios);
  assert(res == 0);
  }

  rfid_eb = evbuffer_new();
  assert(rfid_eb);

  // Pre-allocate enough space
  evbuffer_expand(rfid_eb, 3*DOOR_STRING_MAXLEN);

  // Handle RFID input
  event_assign(&rfid_ev, base, rfid_fd, EV_READ|EV_PERSIST, rfid_rx_cb, NULL);
  event_add(&rfid_ev, NULL);

  // Wake up and probe the magnetic sensor
  timerclear(&door_probe_tv);
  door_probe_tv.tv_usec = 500000; /* Half a second */
  event_assign(&door_probe_ev, base, -1, EV_PERSIST, door_probe_status, NULL);
  event_add(&door_probe_ev, &door_probe_tv);
}

//                                                                      }}}
// On-Air Sign (XXX)                                                    {{{
FILE* ota_gpio_file;
int8_t ota_status = -1;

static int ota_set(int8_t status) {
  if(status) {
#ifdef DOOR_SER_PIN_ONAIR
    serial_mbis(DOOR_SER_PIN_ONAIR);
#endif
  } else {
#ifdef DOOR_SER_PIN_ONAIR
    serial_mbic(DOOR_SER_PIN_ONAIR);
#endif
  }
  
  gpio_write(ota_gpio_file, !status);
  ota_status = status;

  return 0;
}

//                                                                      }}}
// Remote Control                                                       {{{


static int retzero(const unsigned char *_x, void *_ign) {
  return 0;
}

static void remote_rx_cb(struct bufferevent *bev, void *arg) {
  char *line = NULL;

  struct evbuffer *inbev = bufferevent_get_input(bev);
  struct evbuffer *outbev = bufferevent_get_output(bev);

  line = evbuffer_readln(inbev, NULL, EVBUFFER_EOL_CRLF);
  while (line != NULL) {
    char *saveptr = NULL;
    /* Extract initial token */
    char *cmd = strtok_r(line, "\t ", &saveptr);
    if (!cmd) goto next;
    if (!strcasecmp(cmd, "AUTH")) {
      char *email = strtok_r(NULL, "\t ", &saveptr);
      char *passwd = strtok_r(NULL, "\t ", &saveptr);
      if (email != NULL && passwd != NULL) { 
        int res = db_gate_password(email, passwd, retzero, NULL);
        if (res >= 0) {
          evbuffer_add_printf(outbev, "Success\n");
        } else {
          evbuffer_add_printf(outbev, "Failure\n");
        }
      }
    } else if (!strcasecmp(cmd, "OPEN")) {
      char *email = strtok_r(NULL, "\t ", &saveptr);
      char *passwd = strtok_r(NULL, "\t ", &saveptr);
      if (email != NULL && passwd != NULL) { 
        int res = db_gate_password(email, passwd, lock_open, NULL);
        if (res >= 0) {
          evbuffer_add_printf(outbev, "Success\n");
        } else {
          evbuffer_add_printf(outbev, "Failure\n");
        }
      }
    } else if (!strcasecmp(cmd, "ONAIR")) {
        ota_set(1);
    } else if (!strcasecmp(cmd, "OFFAIR")) {
        ota_set(0);
#ifdef DEBUG
    } else if (!strcasecmp(cmd, "RFID")) {
      char *token = strtok_r(NULL, "\t ", &saveptr);
      if (token != NULL) { 
        int res = db_gate_rfid(token, lock_open, NULL);
        if (res >= 0) {
          evbuffer_add_printf(outbev, "Success\n");
        } else {
          evbuffer_add_printf(outbev, "Failure\n");
        }
      }
    } else if (!strcasecmp(cmd, "LOG")) {
      evbuffer_add_printf(outbev, "XXX\n");
#endif
    } else if (!strcasecmp(cmd, "QUIT")) {
#if defined(DEBUG) && defined(DEBUG_QUIT_ON_QUIT)
      event_base_loopbreak(bufferevent_get_base(bev));
#endif
      goto drop;
    } else if (!strcasecmp(cmd, "PING")) {
      evbuffer_add_printf(outbev, "Pong! :)\n");
    } else {
      evbuffer_add_printf(outbev, "Unknown command: %s %s\n", cmd, saveptr);
    }
next:
    saveptr = NULL;
    free(line);
    line = evbuffer_readln(inbev, NULL, EVBUFFER_EOL_CRLF);
  }

  int len = evbuffer_get_length(inbev);
  if(len >= REMOTE_STRING_MAXLEN) {
    /* The remote end is just too rude; be rude back */
    goto drop;
  }
  return;

drop:
  log_printf("Dropping remote connection %d\n", bufferevent_getfd(bev));
  free(line);
  bufferevent_free(bev);
  return;
}

static void remote_event_cb(struct bufferevent *bev, short what, void *arg) {
  // log_printf("Remote event: %d %d\n", bufferevent_getfd(bev), what);

  // Hang up
  bufferevent_free(bev);
}

static void remote_accept(evutil_socket_t fd, short what, void *arg) {
  int cfd = accept(fd, NULL, NULL);
  log_printf("Accepted remote connection %d\n", cfd);

  struct bufferevent *be
    = bufferevent_socket_new((struct event_base *)arg, cfd, BEV_OPT_CLOSE_ON_FREE);
  if(!be)
    goto err;

  bufferevent_setcb(be, remote_rx_cb, NULL, remote_event_cb, NULL);

  struct timeval tv;  
    timerclear(&tv);
  tv.tv_sec = REMOTE_WRITER_TIMEOUT;
  // XXX REMOTE_READER_TIMEOUT?
  bufferevent_set_timeouts(be, NULL, &tv);

  bufferevent_enable(be, EV_READ | EV_WRITE);

  return;
err:
  close(cfd);
  return;
}

//                                                                      }}}
// Utils                                                                {{{

static void setnonblock(int fd)
{
  int fl;
  fl = fcntl(fd, F_GETFL);
  assert(fl >= 0);
  fl |= O_NONBLOCK;
  fl = fcntl(fd, F_SETFL, fl);
  assert(fl >= 0);
}

//                                                                      }}}
// Signal handling                                                      {{{

struct event_base *base;
static void signals_init(int);

static void signals_catch(int s, siginfo_t *si, void *uc) {
  static char msg[] = "Caught signal; requesting event loop exit\n";

  // Reset signal handling so the next one is fatal
  signals_init(0);

  int res = write(2,msg,ASIZE(msg));
  if (res != ASIZE(msg)) {
    // XXX
    // We should do something about that, but what?  If this isn't here,
    // some versions of GCC complain about ignoring write's result.
  }
  
  event_base_loopbreak(base);
}

static void signals_init(int catch) {
  int res;
  struct sigaction sa;
 
  sigemptyset(&sa.sa_mask);
  if (catch) {
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signals_catch;
  } else {
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
  }
  res = sigaction(SIGSEGV, &sa, NULL);
  assert(res != -1);
  res = sigaction(SIGINT, &sa, NULL);
  assert(res != -1);
  res = sigaction(SIGQUIT, &sa, NULL);
  assert(res != -1);
  res = sigaction(SIGTERM, &sa, NULL);
  assert(res != -1);
}


//                                                                      }}}
// Main                                                                 {{{
int main(int argc, char **argv){
  char *door_fn = NULL;
  char *db_fn = NULL;
  char *lock_gpio_fn = NULL;
  char *ota_gpio_fn = NULL;
  int remote_port = -1;

  {
    int opt;
    while((opt = getopt(argc, argv, "A:D:F:L:o:p:")) != -1) {
      switch (opt) {
        case 'A': ota_gpio_fn = optarg; break;
        case 'F': door_fn = optarg; break;
        case 'D': db_fn = optarg; break;
        case 'L': lock_gpio_fn = optarg; break;
        case 'o': 
          lock_time = atoi(optarg);
          if(lock_time <= 0) { lock_time = 30; }
          break;
        case 'p': remote_port = atoi(optarg); break;
        default: printf("Bad argument %c\n", opt); exit(-1);
      }
    }
  }
  if (!door_fn || !db_fn) {
    printf("Please specify -F and -D\n");
    return -1;
  }

  base = event_base_new();
  signals_init(1);

  // Open the serial port for the door and lock
  int ser_fd = open(door_fn, O_RDONLY|O_NONBLOCK);
  assert(ser_fd >= 0);
#ifdef DOOR_USE_SERIAL_PINS
  serial_pin_fd = ser_fd;
#endif

  // Subsystems up!
  log_init();

  if (lock_gpio_fn) {
    log_printf("Using lock GPIO file: %s\n", lock_gpio_fn);
    lock_gpio_file = fopen(lock_gpio_fn, "w");
    assert(lock_gpio_file != NULL);
  }
  if (ota_gpio_fn) {
    log_printf("Using On-The-Air GPIO file: %s\n", ota_gpio_fn);
    ota_gpio_file = fopen(ota_gpio_fn, "w");
    assert(ota_gpio_file != NULL);
  }

  log_printf("Initializing (door open time is %d seconds)...\n", lock_time);
  if(db_init(db_fn)) { return -1; }
  lock_init(base);
  rfid_init(base, ser_fd);

  // XXX oh how I hate all this.  Pasted from numerous examples on the
  // Internet.
  if(remote_port > 0) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(remote_port);
    int reuseaddr_on = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
        sizeof(reuseaddr_on));
    bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    setnonblock(listen_fd);
    listen(listen_fd, 1);
   
    struct event ev_accept;
    event_assign(&ev_accept, base, listen_fd, EV_READ|EV_PERSIST, remote_accept, base);
    event_add(&ev_accept, NULL);
    log_printf(" Opened remote listener on port (%d)...\n", remote_port);
  }

  // Go!
  log_printf("System alive!\n");

  event_base_dispatch(base);

  alarm(5);     /* Give ourselves five seconds to clean up */
  log_printf("Dying...\n");

  // Things just got exciting: we ran out of events to listen to, or were
  // forced out of the event loop.  Time to die so that our supervisor can
  // respawn us.  On the way out, try to clean up a little to ease debugging
  // in things like valgrind.

  serial_deinit(ser_fd); 
  if(lock_gpio_file) { fclose(lock_gpio_file); }
  if(ota_gpio_file) { fclose(ota_gpio_file); }

  evbuffer_free(rfid_eb);
  evbuffer_free(log_eb);
  event_base_free(base);
  db_deinit();
  return 0;
}
//                                                                      }}}

// vim: set foldmethod=marker:ts=2:expandtab
