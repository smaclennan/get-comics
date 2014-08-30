CC = clang -fno-color-diagnostics

# SAM CFLAGS += -O3 -Wall -g
CFLAGS += -Wall -g

# For dependencies
CFLAGS += -Wp,-MD,$(@D)/.$(@F).d

# Comment in to enable the log_* functions
# Currently unused. Mainly for debugging.
#CFLAGS += -DLOGGING

# Comment in to enable https via openssl
CFLAGS += -DWANT_SSL

# Comment in to enable gzip encoding
CFLAGS += -DWANT_GZIP

# Currently I use gccgo
GO=$(shell which gccgo 2>/dev/null)
ifneq ($(GO),)
EXTRA+=go-get-comics
endif

COMMON  := http.o log.o openssl.o socket.o

# Optionaly add openssl
ifneq ($(findstring WANT_SSL,$(CFLAGS)),)
LIBS += -lssl -lcrypto
endif

# Optionaly add gzip
ifneq ($(findstring WANT_GZIP,$(CFLAGS)),)
LIBS += -lz
endif

#
# Pretty print - "borrowed" from sparse Makefile
#
V	      = @
Q	      = $(V:1=)
QUIET_CC      = $(Q:@=@echo    '     CC       '$@;)
QUIET_LINK    = $(Q:@=@echo    '     LINK     '$@;)

%.o: %.c
	$(QUIET_CC)$(CC) -o $@ -c $(CFLAGS) $<

all:	get-comics link-check http-get $(EXTRA)

get-comics: get-comics.o $(COMMON) config.o my-parser.o
	$(QUIET_LINK)$(CC) $(CFLAGS) -o get-comics $+ $(LIBS)
	@if [ -x /usr/bin/etags ]; then /usr/bin/etags *.c *.h; fi

link-check: link-check.o $(COMMON)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o link-check $+ $(LIBS)

http-get: http-get.o $(COMMON)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o http-get $+ $(LIBS)

go-get-comics: get-comics.go
	$(GO) -o $@ $+

*.o: get-comics.h

get-comics.html: get-comics.1
	man2html get-comics.1 > get-comics.html

install: all
	install -D -s -m 755 get-comics $(DESTDIR)/usr/bin/get-comics
	install -D -m 644 get-comics.1 $(DESTDIR)/usr/man/man1/get-comics.1
	gzip $(DESTDIR)/usr/man/man1/get-comics.1
	install -D -m 644 comics.json $(DESTDIR)/usr/share/get-comics/comics.json

clean:
	rm -f get-comics link-check http-get *.o .*.o.d get-comics.html TAGS
	rm -f go-get-comics

include $(wildcard .*.o.d)