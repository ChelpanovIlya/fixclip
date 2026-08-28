CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lX11 -lXfixes
PREFIX = /usr/local

fixclip: fixclip.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: fixclip
	install -Dm755 fixclip $(DESTDIR)$(PREFIX)/bin/fixclip

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/fixclip

clean:
	rm -f fixclip

.PHONY: install uninstall clean
