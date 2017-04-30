VERSION=0.5

CC = clang -fno-color-diagnostics

# If you set D=1 on the command line then $(D:1=-g)
# returns -g, else it returns the default (-O2).
D = -O2
CFLAGS += -Wall $(D:1=-g)

# For dependencies
CFLAGS += -Wp,-MD,$(@D)/.$(@F).d

MAKEFLAGS += --no-print-directory

# Comment in to enable the log_* functions
# Currently unused. Mainly for debugging.
#CFLAGS += -DLOGGING

# Comment in to enable libcurl
#CFLAGS += -DWANT_CURL

# For curl we do not need ssl/gzip
ifeq ($(findstring WANT_CURL,$(CFLAGS)),)
# Comment in to enable https via openssl
CFLAGS += -DWANT_OPENSSL

# Comment in to enable https via mbedtls
#CFLAGS += -DWANT_MBEDTLS

# Comment in to enable gzip encoding
CFLAGS += -DWANT_GZIP

# Comment in for zlib rather than gzip
#CFLAGS += -DWANT_ZLIB
#ZDIR = zlib

# Comment in for persistent connections
CFLAGS += -DREUSE_SOCKET
endif

# Currently I use gccgo
#GO=$(shell which gccgo 2>/dev/null)
#ifneq ($(GO),)
#EXTRA+=go-get-comics
#endif

CFILES  := log.c common.c

# Optionally add openssl
ifneq ($(findstring WANT_OPENSSL,$(CFLAGS)),)
CFLAGS += -DWANT_SSL
LIBS += -lssl -lcrypto
CFILES += openssl.c
endif
# Optionally add mbedtls
ifneq ($(findstring WANT_MBEDTLS,$(CFLAGS)),)
PDIR=mbedtls
CFLAGS += -DWANT_SSL -I$(PDIR)/include
LIBS += -L$(PDIR)/library -lmbedtls -lmbedx509 -lmbedcrypto
CFILES += mbedtls.c
endif

# Optionaly add gzip
ifneq ($(findstring WANT_ZLIB,$(CFLAGS)),)
ZLIB=$(ZDIR)/libz.a
LLIBS += $(ZLIB)
else
ifneq ($(findstring WANT_GZIP,$(CFLAGS)),)
LIBS += -lz
endif
endif

# Optionally add libcurl
ifneq ($(findstring WANT_CURL,$(CFLAGS)),)
LIBS += -lcurl
CFILES += curl.c
else
CFILES += http.c socket.c
endif

LIBS += $(LLIBS)

COMMON	:= $(CFILES:.c=.o)

#
# Pretty print - "borrowed" from sparse Makefile
#
V	      = @
Q	      = $(V:1=)
QUIET_CC      = $(Q:@=@echo    '     CC       '$@;)
QUIET_LINK    = $(Q:@=@echo    '     LINK     '$@;)
QUIET_GO      = $(Q:@=@echo    '     GO       '$@;)

%.o: %.c
	$(QUIET_CC)$(CC) -o $@ -c $(CFLAGS) $<

all:	get-comics link-check http-get $(EXTRA)

get-comics: get-comics.o $(COMMON) config.o my-parser.o $(LLIBS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o get-comics $+ $(LIBS)
	@if [ -x /usr/bin/etags ]; then /usr/bin/etags *.c *.h; fi

link-check: link-check.o $(COMMON) $(LLIBS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o link-check $+ $(LIBS)

http-get: http-get.o $(COMMON) $(LLIBS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o http-get $+ $(LIBS)

go-get-comics: get-comics.go
	$(QUIET_GO)$(GO) -o $@ $+

$(ZLIB):
	@$(MAKE) -C $(ZDIR)

*.o: get-comics.h

get-comics.html: get-comics.1
	man2html get-comics.1 > get-comics.html

check:
	sparse $(CFLAGS) get-comics.c $(CFILES) config.c my-parser.c

tarball: COPYING Makefile README* *.[ch] get-comics.1 comics.json
	mkdir get-comics-$(VERSION)
	cp $+ get-comics-$(VERSION)
	tar zcf slackware/get-comics-$(VERSION).tar.gz get-comics-$(VERSION)
	rm -rf get-comics-$(VERSION)

$(DESTDIR)/usr/share/get-comics/comics.json:
	install -D -m 644 comics.json $(DESTDIR)/usr/share/get-comics/comics.json

install: all $(DESTDIR)/usr/share/get-comics/comics.json
	install -D -s -m 755 get-comics $(DESTDIR)/usr/bin/get-comics
	install -D -m 644 get-comics.1 $(DESTDIR)/usr/man/man1/get-comics.1
	gzip -f $(DESTDIR)/usr/man/man1/get-comics.1

clean:
	rm -f get-comics link-check http-get *.o .*.o.d get-comics.html TAGS
	rm -f go-get-comics
ifneq ($(ZDIR),)
	@make -C $(ZDIR) clean
endif

include $(wildcard .*.o.d)
