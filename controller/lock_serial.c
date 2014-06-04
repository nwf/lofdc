// Serial-based lock                                                    {{{

/* XXX
struct lock_ser_priv {
  FILE *lsp_fd;
  char lsp_pin_lock;
};

static void lock_ser_close(void *_priv) {
  struct lock_ser_priv *priv = _priv;
  serial_mbic(priv->lsp_fd, priv->lsp_pin_lock);
}

static int lock_ser_open(void *_priv) {
  struct lock_ser_priv *priv = _priv;
  serial_mbis(priv->lsp_fd, priv->lsp_pin_lock);
  return 0;
}

static void lock_ser_destroy(void *_priv) {
  struct lock_ser_priv *priv = _priv;
  close(priv->lsp_fd);
}
*/

//                                                                      }}}
