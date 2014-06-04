#ifndef LOFDC_COMMON_UTILS_H
#define LOFDC_COMMON_UTILS_H

/* Serial utilities -- common-utils.c */
void serial_mbis(int fd, int flags);
void serial_mbic(int fd, int flags);
void serial_init(int fd, int cflags);
void serial_deinit(int fd);

/* GPIO utilities -- common-utils.c */
void gpio_write(FILE *f, int fl);

/* FD utilities -- common-utils.c */
void setnonblock(int fd);

/* Tree utilities -- common-utils.c */
void tdestroy_finalize(void **tree, void (*)(void *));

/* Dynlibs -- common-utils.c */
void * load_obj(char *fn, void **odl, char **oerr);

/* Logging utilities -- common-log.c */
struct evbuffer *log_eb;
void log_printf(const char *fmt, ...);

/* Array size macro */
#define ASIZE(n) (sizeof(n)/sizeof(n[0]))

#endif
