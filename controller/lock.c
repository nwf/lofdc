#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include <event2/event.h>
#include <event2/event_struct.h>

#include "controller/common-utils.h"
#include "controller/lock.h"
#include "controller/lock-vtable.h"

// Lock control                                                         {{{

static LIST_HEAD(all_lock, lock_obj) all_lock;
static void __attribute__((constructor)) lock_common_ctor(void) {
  LIST_INIT(&all_lock);
}

static void lock_common_timer(evutil_socket_t fd, short what, void *_arg) {
  struct lock_obj *lo = _arg;

  log_printf("Lock '%s' timeout\n", lo->lo_name);
  lo->lo_vtable->lo_close(lo->lo_priv);
}

int lock_common_open(const unsigned char *who, void *_arg) {
  struct lock_obj *lo = _arg;
  int ret = 0;

  if (event_pending(&lo->lo_tev, EV_TIMEOUT, NULL)) {
    log_printf("Lock '%s' re-opened: '%s'\n", lo->lo_name, who);
    event_add(&lo->lo_tev, &lo->lo_ttv);
  } else {
    ret = lo->lo_vtable->lo_open(lo->lo_priv);
    log_printf("Lock '%s' open: '%s' (ret=%d)\n", lo->lo_name, who, ret);
    if(ret >= 0) {
      event_add(&lo->lo_tev, &lo->lo_ttv);
    }
  }
  return ret;
}

void lock_common_destroy(struct lock_obj *lo) {
  lo->lo_vtable->lo_destroy(lo->lo_priv);

  lo->lo_vtable = NULL;

  event_del(&lo->lo_tev);

  free(lo);
}

struct lock_obj *_lock_common_create(struct event_base *base,
    struct lock_vtable *vt, int to, char *n, void *params
) {
  struct lock_obj *lo = calloc(sizeof(struct lock_obj),1);

  if(!lo) {
    goto out_lo;
  }

  // Allocation-less initializations.
  lo->lo_name = n;
  lo->lo_vtable = vt;
  timerclear(&lo->lo_ttv);
  lo->lo_ttv.tv_sec = to;

  lo->lo_priv = vt->lo_create(params);

  if(!lo->lo_priv) {
	goto out_priv;
  }

  evtimer_assign(&lo->lo_tev, base, lock_common_timer, lo);

  LIST_INSERT_HEAD(&all_lock, lo, lo_all_locks);

  return lo;
out_priv:
  free(lo);
out_lo:
  return NULL;
}

#if 0 // not yet
struct lock_obj *lock_common_create(struct event_base *base,
  int to, char *arg
) {
  char *saveptr  = NULL;
  char *lockname = strtok_r(arg, ":", &saveptr);
  char *lockmod  = strtok_r(NULL, ":", &saveptr);
  char *lockargs = strtok_r(NULL, ":", &saveptr);

  if (!lockargs) {
    return NULL;
  }

  void *dl;
  void *vt = load_obj(lockmod, &dl, NULL);
  struct lock_obj *lo = _lock_common_create(base, vt, to, lockname, lockargs);
  if(lo) {
    lo->lo_dynlib_obj = dl;
  }

  return lo;
}
#endif

void lock_common_finalize_all(void) {
  struct lock_obj *lo;

  while ((lo = all_lock.lh_first) != NULL) {
    LIST_REMOVE(lo, lo_all_locks);
    lock_common_destroy(lo);
  }
}

//                                                                      }}}
