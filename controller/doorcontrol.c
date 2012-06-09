/*
 * Load of Fun Door Controller daemon process
 * Copyright 2012 Nathaniel Wesley Filardo <nwf@ietfng.org>
 *
 * Released under AGPLv3; see COPYING for details.
 */

// Configuration Knobs                                                  {{{
#define DATABASE_FILENAME "door.db"
#define LOCK_OPEN_DURATION 30
#define DOOR_FILENAME "/dev/null"
#define REMOTE_LISTEN_PORT 12321

/* Anybody else who wants to use the store here to authenticate has to use
 * the same PBKDF parameters that we use for hashing.
 */
#define DB_PWHASH_ITERS  1000
#define DB_PWHASH_OUTLEN 20

#define DEBUG
#define DEBUG_CREATE_TEST_USERS

#define DOOR_PIN_LOCK   TIOCM_DTR
#define DOOR_PIN_ONAIR  TIOCM_RTS
//                                                                      }}}
// Prelude                                                              {{{
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
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

#define ASIZE(n) (sizeof(n)/sizeof(n[0]))
//                                                                      }}}
// Writer                                                               {{{

#define WRITER_TIMEOUT 5
struct writer_state {
  int fd;
  struct evbuffer *b;
  struct event e;
  struct event et;
  struct event *ef;
};

static void writer_finish(struct writer_state *s) {
  if (s->ef) {
    /* If we have a continuation event, add it */
    event_add(s->ef, NULL);
  } else {
    /* Otherwise, close up shop */
    close(s->fd);
  }
  event_del(&s->e);
  event_del(&s->et);
  evbuffer_free(s->b);
  free(s);
}

static void writer_timeout(evutil_socket_t fd, short what, void *arg) {
  struct writer_state *s = (struct writer_state *)arg;
  writer_finish(s);  
}

static void writer_tx_cb(evutil_socket_t fd, short what, void *arg) {
  struct writer_state *s = (struct writer_state *)arg;

  assert(s->fd == fd);

  int len = evbuffer_write(s->b, fd);
  if (len <= 0) {
    /* Hang up if we fail to write anything */
    close(fd);
    writer_finish(s);
  } else if(evbuffer_get_length(s->b) == 0) {
    /* We're done; free up our event state and go on our merry way */
    writer_finish(s);
  } else {
    /* Reset the timeout */
    struct timeval tv;  
      timerclear(&tv);
      tv.tv_sec = WRITER_TIMEOUT;
    event_add(&s->et, &tv);
  }
}

static void writer_write_evbuffer(struct event_base *base,
                                  struct event *ev,
                                  int fd, struct evbuffer *b) {
  struct writer_state *s = calloc(sizeof(struct writer_state), 1);
  if (!s) {
    goto err;
  }
  s->fd = fd;
  s->ef = ev;

  s->b = evbuffer_new();
  if (!s->b) {
    goto err_dumpbuf;
  }

  /* Copy the buffer */
  {
    int len = evbuffer_get_length(b);
    evbuffer_add(s->b, evbuffer_pullup(b, len), len);
  }

  /* Construct an event for us, which will gobble itself up */
  event_assign(&s->e, base, fd, EV_WRITE, writer_tx_cb, s);
  event_add(&s->e, NULL);

  /* Also add a timeout to prevent somebody from holding us up too long */
  struct timeval tv;  
    timerclear(&tv);
  tv.tv_sec = WRITER_TIMEOUT;
  evtimer_assign(&s->et, base, writer_timeout, s);
  event_add(&s->et, &tv);

  return;

err_dumpbuf:
  free(s);
err:
  return;  
}


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
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  va_start(ap, fmt);
  vsyslog(LOG_INFO, fmt, ap);
  va_end(ap);
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

static int db_init() {
  int ret;
  char *err;
  const char *tail;
  static const char *db_init_sql =
    "CREATE TABLE IF NOT EXISTS users ("
      "user_id INTEGER PRIMARY KEY ASC,"
      "email TEXT NOT NULL UNIQUE ON CONFLICT ABORT,"
      "pwsalt BLOB NOT NULL,"
      "pw BLOB NOT NULL,"
      "admin BOOLEAN NOT NULL DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS rfid ("
      "rfid_id INTEGER PRIMARY KEY ASC,"
      "user_id INTEGER NOT NULL,"
      "tsalt BLOB NOT NULL,"
      "token TEXT NOT NULL UNIQUE ON CONFLICT ABORT,"
      "FOREIGN KEY (user_id) REFERENCES users(user_id)"
    ");"
    "CREATE TABLE IF NOT EXISTS log ("
      "log_id INTEGER PRIMARY KEY ASC,"
      "user_id INTEGER,"
      "time TEXT,"
      "message TEXT,"
      "FOREIGN KEY (user_id) REFERENCES users(user_id)"
    ");"
#if defined(DEBUG) && defined(DEBUG_CREATE_TEST_USERS)
    "INSERT OR REPLACE INTO users (user_id, email, pwsalt, pw, admin) VALUES"
      "(0, \"admin@example.com\", x'1234', pwhash(x'1234',\"admin\"), 1),"
      "(1, \"user@example.com\", x'2345', pwhash(x'2345',\"user\"), 0);"
    "INSERT OR REPLACE INTO rfid (user_id, tsalt, token) VALUES"
      "(0, x'4567', pwhash(x'4567', \"ADMTOK\")),"
      "(1, x'5678', pwhash(x'5678', \"USRTOK\"));"
#endif
    ;
  ret = sqlite3_open(DATABASE_FILENAME, &db);
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
    "SELECT email FROM users WHERE email = ?2 AND pw = pwhash(users.pwsalt,?1);";
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

static int _db_gated_call(sqlite3_stmt *st, int (*c)(const unsigned char *)) {
  int ret, res = -1;

  ret = sqlite3_step(st);
  if(ret == SQLITE_ROW) {
    res = c(sqlite3_column_text(st, DB_GATE_RESULT_IX));
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

static int db_gate_password(char *e, char *p, int (*c)(const unsigned char *)) {
  int ret;
  sqlite3_stmt *st = db_gate_password_stmt;

  ret = sqlite3_bind_text(st, DB_GATE_PARAM_EMAIL_IX, e, -1, SQLITE_STATIC); 
  assert(ret == SQLITE_OK);
  ret = sqlite3_bind_text(st, DB_GATE_PARAM_SECRET_IX, p, -1, SQLITE_STATIC); 
  assert(ret == SQLITE_OK);

  return _db_gated_call(st, c);
}

static int db_gate_rfid(char *t, int (*c)(const unsigned char *)) {
  int ret;
  sqlite3_stmt *st = db_gate_rfid_stmt;

  ret = sqlite3_bind_text(st, DB_GATE_PARAM_SECRET_IX, t, -1, SQLITE_STATIC); 
  assert(ret == SQLITE_OK);

  return _db_gated_call(st, c);
}


//                                                                      }}}
// Lock control                                                         {{{
static int lock_fd;
static struct event   lock_timeout_ev;
static struct timeval lock_timeout_tv;
static void lock_timeout_cb(evutil_socket_t fd, short what, void *arg) {
  log_printf("Lock timeout\n");
  int flags = DOOR_PIN_LOCK;
  ioctl(lock_fd, TIOCMBIC, &flags);
}

static int lock_open(const unsigned char *n) {
  int flags = DOOR_PIN_LOCK;
  int res = ioctl(lock_fd, TIOCMBIS, &flags);
  log_printf("Lock open: '%s' (%d)\n", n, res);
  event_add(&lock_timeout_ev, &lock_timeout_tv);

  return 0;
}

static void lock_init(struct event_base *base, int fd) {
  lock_fd = fd;

  /* Tell the OS to lower modem control signals on exit,
   * which will re-engage the lock if we crash.
   */
  struct termios tios;
  memset(&tios, 0, sizeof(tios));
  tcgetattr(fd, &tios);
  tios.c_cflag |= HUPCL;
  tcsetattr(fd, TCSADRAIN, &tios);

  /* De-assert DTR to engage the lock */
  int flags = DOOR_PIN_LOCK;
  ioctl(lock_fd, TIOCMBIC, &flags);

  evtimer_assign(&lock_timeout_ev, base, lock_timeout_cb, NULL);
  timerclear(&lock_timeout_tv);
  lock_timeout_tv.tv_sec = LOCK_OPEN_DURATION;
}
//                                                                      }}}
// RFID Control                                                         {{{

static int door_fd;
static struct event     door_ev;
static struct evbuffer *door_eb;

/*
 * A knob to limit the size of our buffer for reading from the RFID device.
 * Should be set to be large enough to contain any valid response, including
 * newline termination.
 */
#define DOOR_STRING_MAXLEN 100

static int door_flags;
#define DOOR_FLAG_RECOVERING 1

static void door_rx_cb(evutil_socket_t fd, short what, void *arg) {
  int ret = 0;
  char *line = NULL;

  ret = evbuffer_read(door_eb, fd, 2*DOOR_STRING_MAXLEN);
  assert(ret > 0);

  do {
    line = evbuffer_readln(door_eb, NULL, EVBUFFER_EOL_CRLF);
    printf("doorR %d %s\n", door_flags, line);
 
    if(line) { 
      if(door_flags & DOOR_FLAG_RECOVERING) {
        printf("doorE recovered (discarding %s)\n", line);
      } else {
        printf("doorF %s (%zd)\n", line, strlen(line));
        db_gate_rfid(line, lock_open);
      }
      door_flags &= ~DOOR_FLAG_RECOVERING;
      free(line);
    }
  } while (line != NULL);

  int len = evbuffer_get_length(door_eb);
  if(len >= DOOR_STRING_MAXLEN) {
    printf("Drop %d\n", len);
    door_flags |= DOOR_FLAG_RECOVERING;
    evbuffer_drain(door_eb, len);
  }
}

static void door_init(struct event_base *base, int fd) {
  door_fd = fd;

  door_eb = evbuffer_new();
  assert(door_eb);

  // Pre-allocate enough space
  evbuffer_expand(door_eb, 3*DOOR_STRING_MAXLEN);

  event_assign(&door_ev, base, door_fd, EV_READ|EV_PERSIST, door_rx_cb, NULL);
  event_add(&door_ev, NULL);
}

//                                                                      }}}
// Remote Control                                                       {{{

#define REMOTE_STRING_MAXLEN 128

static struct evbuffer *remote_msg_success;
static struct evbuffer *remote_msg_failure;

struct remote_state {
  int fd;
  struct evbuffer *b;
  struct event e;
  struct event_base *base;
};

static void remote_state_finish(struct remote_state *s) {
  evbuffer_free(s->b);
  event_del(&s->e);
  free(s);
}

static int retzero(const unsigned char *_x) {
  return 0;
}

static void remote_rx_cb(evutil_socket_t fd, short what, void *arg) {
  int ret = 0;
  char *line = NULL;

  struct remote_state *s = (struct remote_state *)arg;
  assert(s->fd == fd);

  ret = evbuffer_read(s->b, fd, REMOTE_STRING_MAXLEN);
  if (ret <= 0) {
    goto drop;
  }

  do {
    char *saveptr;
    line = evbuffer_readln(s->b, NULL, EVBUFFER_EOL_CRLF);
    if (line != NULL) {
      /* Extract initial token */
      char *cmd = strtok_r(line, "\t ", &saveptr);
      if (!cmd) { continue; }
      if (!strcasecmp(cmd, "AUTH")) {
        char *email = strtok_r(NULL, "\t ", &saveptr);
        char *passwd = strtok_r(NULL, "\t ", &saveptr);
        if (email != NULL && passwd != NULL) { 
          int res = db_gate_password(email, passwd, retzero);
          if (res >= 0) {
            writer_write_evbuffer(s->base, &s->e, fd, remote_msg_success);
          } else {
            writer_write_evbuffer(s->base, &s->e, fd, remote_msg_failure);
          }
          goto busy;
        }
      } else if (!strcasecmp(cmd, "OPEN")) {
        char *email = strtok_r(NULL, "\t ", &saveptr);
        char *passwd = strtok_r(NULL, "\t ", &saveptr);
        if (email != NULL && passwd != NULL) { 
          int res = db_gate_password(email, passwd, lock_open);
          if (res >= 0) {
            writer_write_evbuffer(s->base, &s->e, fd, remote_msg_success);
          } else {
            writer_write_evbuffer(s->base, &s->e, fd, remote_msg_failure);
          }
          goto busy;
        }
#ifdef DEBUG
      } else if (!strcasecmp(cmd, "RFID")) {
        char *token = strtok_r(NULL, "\t ", &saveptr);
        if (token != NULL) { 
          int res = db_gate_rfid(token, lock_open);
          if (res >= 0) {
            writer_write_evbuffer(s->base, &s->e, fd, remote_msg_success);
          } else {
            writer_write_evbuffer(s->base, &s->e, fd, remote_msg_failure);
          }
          goto busy;
        }
      } else if (!strcasecmp(cmd, "LOG")) {
        writer_write_evbuffer(s->base, &s->e, fd, log_eb);
        goto busy;
#endif
      } else if (!strcasecmp(cmd, "QUIT")) {
#ifdef DEBUG
        event_base_loopbreak(s->base);
#endif
        goto drop;
      }
      saveptr = NULL;
      free(line);
    }
  } while (line != NULL);

  int len = evbuffer_get_length(s->b);
  if(len >= REMOTE_STRING_MAXLEN) {
    goto drop;
  }
  event_add(&s->e, NULL);
  return;

busy:
  free(line);
  return;

drop:
  log_printf("Dropping remote connection %d\n", fd);
  free(line);
  close(fd);
  remote_state_finish(s);
  return;
}

static void remote_accept(evutil_socket_t fd, short what, void *arg) {
  int cfd = accept(fd, NULL, NULL);
  log_printf("Accepted remote connection %d\n", cfd);

  struct remote_state *s = calloc(sizeof(struct remote_state),1);
  if(!s)
    goto err;
  s->base = (struct event_base *)arg;
  s->fd = cfd;
  s->b = evbuffer_new();
  if(!s->b)
    goto err_s;

  event_assign(&s->e, s->base, cfd, EV_READ, remote_rx_cb, s);
  event_add(&s->e, NULL);


  return;

err_s:
  free(s);
err:
  close(cfd);
  return;
}

static void remote_init(void) {
  remote_msg_success = evbuffer_new();
  evbuffer_add_printf(remote_msg_success, "Success\n");

  remote_msg_failure = evbuffer_new();
  evbuffer_add_printf(remote_msg_failure, "Failure\n");
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
// Main                                                                 {{{

int main(int argc, char **argv){
  struct event_base *base = event_base_new();

  // Open the serial port for the door and lock
  int fd = open(DOOR_FILENAME, O_RDONLY|O_NONBLOCK);
  assert(fd >= 0);

  // Subsystems up!
  log_init();
  log_printf("Initializing...\n");
  remote_init();
  if(db_init()) { return -1; }
  lock_init(base, fd);
  door_init(base, fd);

  // XXX oh how I hate all this.  Pasted from numerous examples on the
  // Internet.
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in listen_addr;
  memset(&listen_addr, 0, sizeof(listen_addr));
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_addr.s_addr = INADDR_ANY;
  listen_addr.sin_port = htons(REMOTE_LISTEN_PORT);
  int reuseaddr_on = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, 
      sizeof(reuseaddr_on));
  bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
  setnonblock(listen_fd);
  listen(listen_fd, 1);

  struct event ev_accept;
  event_assign(&ev_accept, base, listen_fd, EV_READ|EV_PERSIST, remote_accept, base);
  event_add(&ev_accept, NULL);

  // Go!
  log_printf("System alive!\n");

  event_base_dispatch(base);

  // Things just got exciting: we ran out of events to listen to, or were
  // forced out of the event loop.  Time to die so that our supervisor can
  // respawn us.  On the way out, try to clean up a little to ease debugging
  // in things like valgrind.
 
  close(fd);
  evbuffer_free(door_eb);
  event_base_free(base);
  db_deinit();
  return 0;
}
//                                                                      }}}

// vim: set foldmethod=marker:ts=2:expandtab
