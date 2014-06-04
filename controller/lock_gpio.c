#include <stdio.h>
#include <stdlib.h>
#include "controller/common-utils.h"
#include "controller/lock-vtable.h"

// GPIO-based lock                                                      {{{

struct lock_gpio_priv {
  FILE *lgp_file;
  char lgp_openstate;
};

static void lock_gpio_close(void *_priv) {
  struct lock_gpio_priv *priv = _priv;
  gpio_write(priv->lgp_file, !priv->lgp_openstate);
}

static int lock_gpio_open(void *_priv) {
  struct lock_gpio_priv *priv = _priv;
  gpio_write(priv->lgp_file, priv->lgp_openstate);
  return 0;
}

static void lock_gpio_destroy(void *_priv) {
  struct lock_gpio_priv *priv = _priv;
  fclose(priv->lgp_file);
  free(priv);
}

static void * lock_gpio_create(void *params) {
  struct lock_gpio_priv *priv = calloc(sizeof(struct lock_gpio_priv), 1);
  if(!priv) {
    goto out_priv;
  }

  priv->lgp_openstate = 0;	// XXX
  priv->lgp_file = params;

  return priv;

out_priv:
  return NULL;
}

struct lock_vtable _plugin_vtable = {
  .lo_create  = lock_gpio_create,
  .lo_destroy = lock_gpio_destroy,

  .lo_open    = lock_gpio_open,
  .lo_close   = lock_gpio_close,
};

//                                                                      }}}

