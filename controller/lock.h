#ifndef CONTROLLER_LOCK_H
#define CONTROLLER_LOCK_H

#include <sys/queue.h>

struct lock_obj {
    /* Initialized and manipulated by common infrastructure */
    struct event   lo_tev;
    struct timeval lo_ttv;

    /* Per-type constructor initialization */
    char          *lo_name;
    void          *lo_priv;

    struct lock_vtable *lo_vtable;

    LIST_ENTRY(lock_obj) lo_all_locks;
};

struct lock_obj *_lock_common_create(struct event_base *base,
    struct lock_vtable *vt, int to, char *n, void *params);

#if 0 // not yet
struct lock_obj *lock_common_create(struct event_base *base,
  int to, char *arg);
#endif

void lock_common_destroy(struct lock_obj *lo);

int lock_common_open(const unsigned char *name, void *_arg);

void lock_common_finalize_all(void);

#endif
