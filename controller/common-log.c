#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <event2/buffer.h>

#include "controller/common-log.h"

/* Where, in addition to the built-in ring buffer, should we log? */
#undef  LOG_TO_SYSLOG
#define LOG_TO_STDERR

/* The one program-wide ring buffer */
#define LOG_TARGET_SIZE 1024
struct evbuffer *log_eb;

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

void log_printf(const char *fmt, ...) {
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

static void __attribute__((constructor)) log_ctor() {
  log_eb = evbuffer_new();
  assert(log_eb);

  struct evbuffer_cb_entry *log_eb_cbe;
  log_eb_cbe = evbuffer_add_cb(log_eb, eb_trim_cb, (void *)LOG_TARGET_SIZE);
  assert(log_eb_cbe);

  openlog("doorcontrol", LOG_PID, LOG_DAEMON);
}
//                                                                      }}}
