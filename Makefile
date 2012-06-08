TARGETS=doorcontrol

CFLAGS=-g -O2 --std=gnu99 -Wall -Werror
LDFLAGS=-lsqlite3 -levent_core

all: $(TARGETS)

clean:
	rm -f $(TARGETS) $(TARGETS:%=%.o)
