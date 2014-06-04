#ifndef _LOFDC_LOCK_VTABLE_H_
#define _LOFDC_LOCK_VTABLE_H_

struct lock_vtable {
    // XXX? void *(*lo_parse)(char *params); ?

	void* (*lo_create )(void *params);
    void  (*lo_destroy)(void *priv);

    int   (*lo_open   )(void *priv);
    void  (*lo_close  )(void *priv);
};

#endif
