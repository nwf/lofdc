#ifndef _LOFDC_RFID_VTABLE_H_
#define _LOFDC_RFID_VTABLE_H_

struct rfid_obj;

struct rfid_vtable {
    void (*ro_create )(struct event_base *, struct rfid_obj *, void *param,
                       void **priv);

    void (*ro_destroy)(void *);
    void (*ro_cron   )(struct rfid_obj *);
};

#endif
