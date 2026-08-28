CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lX11 -lXfixes
PREFIX = /usr/local

GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
BUILD_DATE := $(shell date '+%Y-%m-%d %H:%M:%S')

fixclip: fixclip.c
	$(CC) $(CFLAGS) -DGIT_HASH='"$(GIT_HASH)"' -DBUILD_DATE='"$(BUILD_DATE)"' -o $@ $< $(LDFLAGS)

install: fixclip
	install -Dm755 fixclip $(DESTDIR)$(PREFIX)/bin/fixclip

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fixclip

clean:
	rm -f fixclip

.PHONY: install uninstall clean
