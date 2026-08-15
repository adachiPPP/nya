CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
PKG_CFLAGS := $(shell pkg-config --cflags libcurl libzstd liblzma zlib 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs libcurl libzstd liblzma zlib 2>/dev/null)
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:.c=.o)

nya: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(PKG_LIBS)

%.o: %.c src/nya.h
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c -o $@ $<

install: nya
	install -Dm755 nya $(DESTDIR)/usr/local/bin/nya

clean:
	rm -f nya $(OBJS)

test: nya
	bash tests/run.sh

.PHONY: clean install test
