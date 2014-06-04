TARGETS= \
	controller/doorcontrol \
    controller/doormgmt \
    controller/lock_gpio.so \
    controller/rfid_parallax.so \
	misc/gpiotest \

CFLAGS+=-g -O0 --std=gnu99 -Wall -Werror -I.
LDFLAGS+=-lsqlite3 -levent_core -lcrypto -ldl

all: $(TARGETS)

%.so: %.c
	gcc ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -shared -o $@ -fPIC $^

controller/doorcontrol: CFLAGS+=-rdynamic
controller/doorcontrol: controller/common-db.o \
                        controller/common-log.o \
                        controller/common-utils.o \
                        controller/lock.o \
                        controller/rfid.o \
                        controller/rfid_parallax.o 

controller/doormgmt: controller/common-db.o

clean:
	rm -f $(TARGETS) controller/*.o
