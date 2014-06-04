
// Dynlibs                                                              {{{

void * load_obj(char *fn, void **odl, char **oerr) {
  void *dl;
  char *err;

  dl = dlopen(fn, RTLD_LOCAL | RTLD_LAZY);
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

  return vt;

out_sym:
  dlclose(dl);
out_dl:
  return NULL;
}

//                                                                      }}}
