/*
 * Load of Fun Door Controller daemon process
 * Copyright 2012 Nathaniel Wesley Filardo <nwf@ietfng.org>
 *
 * Released under AGPLv3; see COPYING for details.
 */

// Configuration Knobs (i.e. symbols for mysterious constants)          {{{
#define REMOTE_WRITER_TIMEOUT 30
#define REMOTE_STRING_MAXLEN 128

/* For how long should the lock stay open, in seconds? */
#define LOCK_DEFAULT_TIME  10

/* Features intended for debugging */
#define DEBUG
/* Cause the remote control QUIT command to actually quit the program */
#undef DEBUG_QUIT_ON_QUIT

#define DOOR_TERMIOS_CFLAGS B2400
/* Some pins may be on the attached serial line */
// #define DOOR_SER_PIN_LOCK   TIOCM_DTR
// #define DOOR_SER_PIN_OPENED TIOCM_CTS

//                                                                      }}}
// Prelude                                                              {{{
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/queue.h>

#include <sqlite3.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/listener.h>

#include "controller/common-db.h"
#include "controller/common-log.h"
#include "controller/common-utils.h"
#include "controller/lock.h"
#include "controller/lock-vtable.h"
#include "controller/rfid.h"
#include "controller/rfid-vtable.h"

struct event_base *base;

struct lock_obj *the_lock; // XXX Only one right now

//                                                                      }}}
// Database                                                             {{{

sqlite3 *db;

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

  db_init_pwhash(db);
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
    "  AND pw = pwhash(users.pwsalt,?1);";
  ret = sqlite3_prepare_v2(db, db_gate_password_sql, ASIZE(db_gate_password_sql),
                               &db_gate_password_stmt, &tail);
   if (ret != SQLITE_OK || *tail != '\0') {
    fprintf(stderr, "Can't prepare database: %s (%s)\n", sqlite3_errmsg(db), tail);
    goto err;
  }

#define DB_GATE_PARAM_RFID_TY_IX 3
static const char db_gate_rfid_sql[] =
    "SELECT email FROM users JOIN rfid USING (user_id)"
    " WHERE rfid_type = ?3"
    "  AND token = ?1;";
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

static int db_gate_rfid(enum rfid_token_type tok_ty, char *t,
                        int (*c)(const unsigned char *, void *), void *arg) {
  int ret;
  sqlite3_stmt *st = db_gate_rfid_stmt;

  ret = sqlite3_bind_text(st, DB_GATE_PARAM_SECRET_IX, t, -1, SQLITE_STATIC); 
  assert(ret == SQLITE_OK);
  ret = sqlite3_bind_int(st, DB_GATE_PARAM_RFID_TY_IX, tok_ty); 
  assert(ret == SQLITE_OK);

  return _db_gated_call(st, c, arg);
}

//                                                                      }}}
// Signal handling                                                      {{{

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
// RFID Control (stale)                                                 {{{

#if 0
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
#endif

#if 0
static void rfid_init(struct event_base *base, int fd) {
  rfid_fd = fd;

  serial_init(fd, DOOR_TERMIOS_CFLAGS);

  rfid_eb = evbuffer_new();
  assert(rfid_eb);

  // Pre-allocate enough space
  evbuffer_expand(rfid_eb, 3*DOOR_STRING_MAXLEN);

  // Handle RFID input
  event_assign(&rfid_ev, base, rfid_fd, EV_READ|EV_PERSIST, rfid_rx_cb, NULL);
  event_add(&rfid_ev, NULL);

#if 0
  // Wake up and probe the magnetic sensor
  timerclear(&door_probe_tv);
  door_probe_tv.tv_usec = 500000; /* Half a second */
  event_assign(&door_probe_ev, base, -1, EV_PERSIST, door_probe_status, NULL);
  event_add(&door_probe_ev, &door_probe_tv);
#endif
}
#endif

//                                                                      }}}
// Remote Control                                                       {{{

static LIST_HEAD(all_remote, remote_state) all_remote;
static void __attribute__((constructor)) all_remote_ctor(void) {
  LIST_INIT(&all_remote);
}

struct remote_state {
  struct bufferevent *rs_be;	// For destructor

  char *auth_email; // non-NULL caches last result of AUTH command

  LIST_ENTRY(remote_state) rs_all_remotes;
};

static void remote_set_email(struct remote_state *rs, char *email) {
  if(rs->auth_email) {
    free(rs->auth_email);
  }
  rs->auth_email = email;
}

static void remote_state_free(struct remote_state *rs) {
  bufferevent_free(rs->rs_be);
  remote_set_email(rs, NULL);
  free(rs);
}

static int retzero(const unsigned char *_x, void *_ign) {
  return 0;
}

static void remote_rx_cb(struct bufferevent *bev, void *arg) {
  char *line = NULL;

  struct remote_state *rs = arg;
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
          char *email_copy = strdup(email);
          if (email_copy) {
            evbuffer_add_printf(outbev, "Success\n");
            remote_set_email(rs, email_copy);
          } else {
            evbuffer_add_printf(outbev, "Fail: ENOMEM\n");
          }
        } else {
          evbuffer_add_printf(outbev, "Failure\n");
        }
      }
    } else if (!strcasecmp(cmd, "DEAUTH")) {
      remote_set_email(rs, NULL);
    } else if (!strcasecmp(cmd, "WHOAMI")) {
      if(rs->auth_email) {
        evbuffer_add_printf(outbev, "I believe you to be: %s\n", rs->auth_email);
      } else {
        evbuffer_add_printf(outbev, "You aren't.\n");
      }
    } else if (!strcasecmp(cmd, "OPEN")) {
      char *which = strtok_r(NULL, "\t ", &saveptr);
      char *email = strtok_r(NULL, "\t ", &saveptr);
      char *passwd = strtok_r(NULL, "\t ", &saveptr);
      if (which != NULL && email != NULL && passwd != NULL) { 
        int res = db_gate_password(email, passwd, lock_common_open, the_lock);
        if (res >= 0) {
          evbuffer_add_printf(outbev, "Success\n");
        } else {
          evbuffer_add_printf(outbev, "Failure\n");
        }
      }
      // XXX: Add support for pre-authenticated open commands?
    } else if (!strcasecmp(cmd, "ONAIR")) {
      // XXX
    } else if (!strcasecmp(cmd, "OFFAIR")) {
      // XXX
#ifdef DEBUG
    } else if (!strcasecmp(cmd, "RFID")) {
      char *token = strtok_r(NULL, "\t ", &saveptr);
      if (token != NULL) { 
        int res = db_gate_rfid(RFID_TOKTY_PARALLAX, token, lock_common_open, the_lock);
        if (res >= 0) {
          evbuffer_add_printf(outbev, "Success\n");
        } else {
          evbuffer_add_printf(outbev, "Failure\n");
        }
      }
#endif
#ifdef DEBUG
    } else if (!strcasecmp(cmd, "LOG")) {
      evbuffer_add_printf(outbev, "----- BEGIN LOG -----\n");
      evbuffer_add_buffer_reference(outbev, log_eb);
      evbuffer_add_printf(outbev, "----- END LOG -----\n");
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
  LIST_REMOVE(rs, rs_all_remotes);
  remote_state_free(rs);
  return;
}

static void remote_event_cb(struct bufferevent *bev, short what, void *arg) {
  // log_printf("Remote event: %d %d\n", bufferevent_getfd(bev), what);

  // Hang up
  LIST_REMOVE((struct remote_state *)arg, rs_all_remotes);
  remote_state_free(arg);
}

static void remote_accept(struct evconnlistener *ecl,
                          evutil_socket_t cfd,
                          struct sockaddr *_ra,
                          int _ralen,
                          void *arg) {
  log_printf("Accepted remote connection %d\n", cfd);

  struct remote_state *rs = calloc(1,sizeof(struct remote_state));
  if(!rs)
   goto err_rs;

  struct bufferevent *be
    = bufferevent_socket_new((struct event_base *)arg, cfd, BEV_OPT_CLOSE_ON_FREE);
  if(!be)
    goto err_be;

  rs->rs_be = be;

  bufferevent_setcb(be, remote_rx_cb, NULL, remote_event_cb, rs);

  struct timeval tv;  
  timerclear(&tv);
  tv.tv_sec = REMOTE_WRITER_TIMEOUT;
  // XXX REMOTE_READER_TIMEOUT?
  bufferevent_set_timeouts(be, NULL, &tv);

  bufferevent_enable(be, EV_READ | EV_WRITE);

  LIST_INSERT_HEAD(&all_remote, rs, rs_all_remotes);

  return;
err_be:
  free(rs);
err_rs:
  close(cfd);
  return;
}

//                                                                      }}}
// Main                                                                 {{{

struct lock_rfid_cb_args {
  enum rfid_token_type tokty;
  struct lock_obj *lo;
};

void lock_rfid_cb(char *line, void *_a) {
  struct lock_rfid_cb_args *a = _a;
  db_gate_rfid(a->tokty, line, lock_common_open, a->lo);
}

int main(int argc, char **argv){
  char *rfid_fn = NULL;
  char *db_fn = NULL;
  char *lock_gpio_fn = NULL;
  // XXX char *ota_gpio_fn = NULL;
  int lock_time = 30;
  int remote_port = -1;

  struct evconnlistener *ecl = NULL;

  // XXX struct lock_obj *ota_obj = NULL;

  {
    int opt;
    while((opt = getopt(argc, argv, "A:D:F:L:o:p:")) != -1) {
      switch (opt) {
        // XXX case 'A': ota_gpio_fn = optarg; break;
        case 'F': rfid_fn = optarg; break;
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
#if 0
  if (!rfid_fn || !db_fn) {
    printf("Please specify -F and -D\n");
    return -1;
  }
#endif

  base = event_base_new();
  signals_init(1);

  log_printf("Initializing (door open time is %d seconds)...\n", lock_time);
  if(db_init(db_fn)) { return -1; }

  // Subsystems up!
  if (lock_gpio_fn) {
    FILE *lock_gpio_file;

    log_printf("Using lock GPIO file: %s\n", lock_gpio_fn);
    lock_gpio_file = fopen(lock_gpio_fn, "w");
    assert(lock_gpio_file != NULL);

    struct lock_vtable *lvt = load_obj("lock_gpio.so", NULL, NULL);
    assert(lvt);

    the_lock = _lock_common_create(base, lvt, lock_time, "default", lock_gpio_file);
    assert(the_lock != NULL);
  }

#if 0
  if (ota_gpio_fn) {
    FILE *ota_gpio_file;

    log_printf("Using On-The-Air GPIO file: %s\n", ota_gpio_fn);
    ota_gpio_file = fopen(ota_gpio_fn, "w");
    assert(ota_gpio_file != NULL);

    // XXX ota_obj = lock_gpio_create(base, "OTA", 10 /* XXX */, ota_gpio_file, 0);
    assert(ota_obj != NULL);
  }
#endif

  if(remote_port > 0) {
    struct sockaddr_in listen_addr;
    memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(remote_port);
 
    ecl = evconnlistener_new_bind(base, remote_accept, base,
                                  LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, 5,
                                  (const struct sockaddr *)&listen_addr,
                                  sizeof(listen_addr));
  
    log_printf(" Opened remote listener on port (%d)...\n", remote_port);
  }

  // XXX
  struct lock_rfid_cb_args roa = { RFID_TOKTY_PARALLAX, the_lock };
  void *rpo;
  void *rvt = load_obj("./controller/rfid_parallax.so", &rpo, NULL);
  struct rfid_obj *ro = _rfid_common_create(base, rvt,
                           lock_rfid_cb, &roa, 5, "rfid0", rfid_fn);
  assert(ro);

  // Go!
  log_printf("System alive!\n");

  event_base_dispatch(base);

  alarm(5);     /* Give ourselves five seconds to clean up */
  log_printf("Dying...\n");

  // Things just got exciting: we ran out of events to listen to, or were
  // forced out of the event loop.  Time to die so that our supervisor can
  // respawn us.  On the way out, try to clean up a little to ease debugging
  // in things like valgrind.

  lock_common_finalize_all();
  rfid_common_finalize_all();

  dlclose(rpo);

  if(ecl) { evconnlistener_free(ecl); }

  {
    struct remote_state *rs;
    while ((rs = all_remote.lh_first) != NULL) {
      LIST_REMOVE(rs, rs_all_remotes);
      remote_state_free(rs);
    }
  }

  evbuffer_free(log_eb);
  event_base_free(base);
  libevent_global_shutdown();

  db_deinit();

  return 0;
}
//                                                                      }}}

// vim: set foldmethod=marker:ts=2:expandtab
