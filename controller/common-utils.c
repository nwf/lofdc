#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <search.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "controller/common-utils.h"

// Serial Utilities                                                     {{{

void serial_mbis(int fd, int flags) {
  int res = ioctl(fd, TIOCMBIS, &flags);
  assert(res == 0);
}

void serial_mbic(int fd, int flags) {
  int res = ioctl(fd, TIOCMBIC, &flags);
  assert(res == 0);
}

void serial_init(int fd, int baudtermio) {
  int res;

  struct termios tios;
  memset(&tios, 0, sizeof(tios));
  res = tcgetattr(fd, &tios);
  assert(res == 0);
  tios.c_cflag &= ~(CRTSCTS|CBAUD|CBAUDEX);
  tios.c_cflag |= HUPCL | CLOCAL | baudtermio;
  res = tcsetattr(fd, TCSADRAIN, &tios);
  assert(res == 0);
}

void serial_deinit(int fd) {
  /* 
   * Try to set sane tty flags, notably including B0
   */
  tcflush(fd, TCIOFLUSH);

  struct termios tios;
  memset(&tios, 0, sizeof(tios));
  tcgetattr(fd, &tios);
  tios.c_cflag &= ~(CBAUD|CBAUDEX|CRTSCTS);
  tios.c_cflag |= HUPCL | CLOCAL | B0;
  tcsetattr(fd, TCSAFLUSH, &tios);

  close(fd);
}
//                                                                      }}}
// GPIO utilities                                                       {{{

void gpio_write(FILE *f, int fl) {
  if(f) {
    int res = fprintf(f, fl ? "1\n" : "0\n");
    assert(res == 2);
    res = fflush(f);
    assert(res == 0);
  } 
}

//                                                                      }}}
// Dynlibs                                                              {{{

void * load_obj(char *fn, void **odl, char **oerr) {
  void *dl;
  char *err;

  dlerror(); /* Clear any error */

  dl = dlopen(fn, RTLD_LOCAL | RTLD_NOW);
  if (!dl) {
    if(oerr) { *oerr = dlerror(); }
    goto out_dl;
  }

  if(odl) { *odl = dl; }

  dlerror(); /* Clear any error */

  void *vt = dlsym(dl, "_plugin_vtable");
  err = dlerror();
  if(err) {
    if(oerr) { *oerr = err; }
    goto out_sym;
  }

#if 0 // not yet
  if(*(uint64_t *)(vt) != magic) {
    if(oerr) { *oerr = "Incorrect magic"; }
    goto out_sym;
  }
#endif

  return vt;

out_sym:
  dlclose(dl);
out_dl:
  return NULL;
}

//                                                                      }}}
// Utils                                                                {{{

void setnonblock(int fd)
{
  int fl;
  fl = fcntl(fd, F_GETFL);
  assert(fl >= 0);
  fl |= O_NONBLOCK;
  fl = fcntl(fd, F_SETFL, fl);
  assert(fl >= 0);
}

void tdestroy_finalize(void **tree, void (*cb)(void *)) {
  void *root = *tree;
  *tree = NULL;
  tdestroy(root, cb);
}

//                                                                      }}}
// vim: set foldmethod=marker:ts=2:expandtab
