#ifndef CONTROLLER_RFID_H
#define CONTROLLER_RFID_H

#include <sys/queue.h>

struct rfid_obj {
  char *ro_name;
  struct rfid_vtable *ro_vtable;

  void (*ro_linecb)(char *, void *);
  void *ro_linecb_priv;

  void *ro_priv;

  /* RFID objects can run periodic callbacks, which get a pointer to
   * the whole rfid_obj structure as its argument.
   */
  struct event        ro_tev;
  struct timeval      ro_ttv;

  /* Linkage for destructor */
  LIST_ENTRY(rfid_obj) ro_all_rfids;
};

struct rfid_obj *_rfid_common_create(struct event_base *base,
  struct rfid_vtable *vt,
  void (*)(char *, void *), void *,
  int cp, char *n, void *param);

#if 0 // not yet
struct rfid_obj *rfid_common_create(struct event_base *base,
  void (*)(char *, void *), void *,
  int to, char *arg);
#endif

void rfid_common_destroy(struct rfid_obj *ro);

void rfid_common_finalize_all(void);



#endif
