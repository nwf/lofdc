TARGETS= \
	controller/doorcontrol \
    controller/doormgmt \
	misc/gpiotest \

CFLAGS+=-g -O2 --std=gnu99 -Wall -Werror -I.
LDFLAGS+=-lsqlite3 -levent_core -lcrypto

all: $(TARGETS)

controller/doorcontrol: controller/common-db.o
controller/doormgmt: controller/common-db.o

clean:
	rm -f $(TARGETS) $(TARGETS:%=%.o) controller/common-db.o
