#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include "controller/rfid.h"
#include "controller/rfid-vtable.h"

/*
 * A knob to limit the size of our buffer for reading from the RFID device.
 * Should be set to be large enough to contain any valid response, including
 * newline termination.
 */
#define DOOR_STRING_MAXLEN 12

/* tcsendbreak() duration; on Linux this means milliseconds if nonzero */
#define RFID_SEND_BREAK_DURATION 400

struct priv {
  enum {
    FLAG_RECOVERING = 1
  } flags;
  int break_duration;
  struct bufferevent *bev;
};

static void rfid_rx_cb(struct bufferevent *bev, void *_arg) {
  char *line = NULL;
  struct rfid_obj *self = _arg;
  struct priv *priv = self->ro_priv;

  assert(bev == priv->bev);

  struct evbuffer* evb = bufferevent_get_input(bev);
  do {
    line = evbuffer_readln(evb, NULL, EVBUFFER_EOL_ANY);
 
    if(line) { 
#if 0 // XXX
      tcsendbreak(fd,RFID_SEND_BREAK_DURATION);
#endif

      if(priv->flags & FLAG_RECOVERING) {
        ; /* Gobble line */
      } else {
        self->ro_linecb(line, self->ro_linecb_priv);
      }
      priv->flags &= ~FLAG_RECOVERING;
      free(line);
    }
  } while (line != NULL);

  int len = evbuffer_get_length(evb);
  if(len >= DOOR_STRING_MAXLEN) {
    priv->flags |= FLAG_RECOVERING;
    evbuffer_drain(evb, len);
  }
}

static void rfid_destroy(void *_priv) {
  struct priv *priv = _priv;

  bufferevent_flush(priv->bev, EV_READ|EV_WRITE, BEV_FINISHED);
  bufferevent_free(priv->bev);
  free(priv);
}

static void rfid_create(struct event_base *base,
                        struct rfid_obj *ro,
                        void *args,
                        void **_priv) {
  *_priv = NULL;

  *_priv = calloc(sizeof(struct priv), 1);
  if(!*_priv) {
    goto out_priv;
  }

  struct priv *priv = *_priv;

  int fd = open(args, O_RDONLY);
  if(fd < 0) {
    perror("Unable to open");
    goto out_fd;
  }

  priv->bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);
  if(!priv->bev) {
    printf("Can't allocate bufferevent\n");
    goto out_bev;
  }

  bufferevent_setcb(priv->bev, rfid_rx_cb, NULL, NULL, ro);
  bufferevent_enable(priv->bev, EV_READ);

  return;

out_bev:
  close(fd);
out_fd:
  free(*_priv);
out_priv:
  return;
}

struct rfid_vtable _plugin_vtable = {
  .ro_cron = NULL,  // These kinds of RFID do not need periodic events
  .ro_create = rfid_create,
  .ro_destroy = rfid_destroy,
}; 
