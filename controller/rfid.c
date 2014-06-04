#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/event_struct.h>

#include "controller/common-utils.h"
#include "controller/rfid.h"
#include "controller/rfid-vtable.h"

// Generic infrastructure                                               {{{

static LIST_HEAD(all_rfid, rfid_obj) all_rfid;
static void __attribute__((constructor)) rfid_common_ctor(void) {
  LIST_INIT(&all_rfid);
}

void rfid_common_destroy(struct rfid_obj *ro) {
  // Stop periodic callbacks
  if(ro->ro_vtable->ro_cron) {
    event_del_block(&ro->ro_tev);
  }

  if(ro->ro_vtable->ro_destroy) {
    ro->ro_vtable->ro_destroy(ro->ro_priv);
  }

  free(ro);
}

void rfid_common_timer(evutil_socket_t fd, short what, void *arg) {
  struct rfid_obj *ro = arg;
  ro->ro_vtable->ro_cron(ro);

  event_add(&ro->ro_tev, &ro->ro_ttv);
}

struct rfid_obj *_rfid_common_create(struct event_base *base,
    struct rfid_vtable *vt,
    void (*cb)(char *, void *), void *cbp,
    int cp, char *n, void *params
) {
  struct rfid_obj *ro = calloc(sizeof(struct rfid_obj),1);

  if(!ro) {
    goto out_ro;
  }

  // Allocation-less initializations.
  ro->ro_name = n;
  ro->ro_vtable = vt;
  timerclear(&ro->ro_ttv);
  ro->ro_ttv.tv_sec = cp;

  ro->ro_linecb = cb;
  ro->ro_linecb_priv = cbp;

  evtimer_assign(&ro->ro_tev, base, rfid_common_timer, ro);

  vt->ro_create(base, ro, params, &ro->ro_priv);

  if(!ro->ro_priv) {
	goto out_priv;
  }

  if(vt->ro_cron) {
    event_add(&ro->ro_tev, &ro->ro_ttv);
  }

  LIST_INSERT_HEAD(&all_rfid, ro, ro_all_rfids);

  return ro;
out_priv:
  free(ro);
out_ro:
  return NULL;
}

#if 0 // not yet
struct rfid_obj *rfid_common_create(struct event_base *base,
    void (*cb)(char *, void *), void *cbp, int cp, char *arg
) {
  char *saveptr  = NULL;
  char *rfidname = strtok_r(arg, ":", &saveptr);
  char *rfidmod  = strtok_r(NULL, ":", &saveptr);
  char *rfidargs = strtok_r(NULL, ":", &saveptr);

  if (!rfidargs) {
    return NULL;
  }

  void *dl;
  void *vt = load_obj(rfidmod, &dl, NULL);

  if(!vt) {
    return NULL;
  }

  struct rfid_obj *ro = _rfid_common_create(base, vt, cb, cbp, cp,
                                            rfidname, rfidargs);
  if(ro) {
    ro->ro_dynlib_obj = dl;
  }

  return ro;
}
#endif


void rfid_common_finalize_all(void) {
  struct rfid_obj *ro;

  while ((ro = all_rfid.lh_first) != NULL) {
    LIST_REMOVE(ro, ro_all_rfids);
    rfid_common_destroy(ro);
  }

}

//                                                                      }}}
