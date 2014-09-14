CC = clang -fno-color-diagnostics

# If you set D=1 on the command line then $(D:1=-g)
# returns -g, else it returns the default (-O2).
D = -O2
CFLAGS += -Wall $(D:1=-g)

# For dependencies
CFLAGS += -Wp,-MD,$(@D)/.$(@F).d

# Comment in to enable the log_* functions
# Currently unused. Mainly for debugging.
#CFLAGS += -DLOGGING

# Comment in to enable https via openssl
#CFLAGS += -DWANT_OPENSSL

# Comment in to enable https via polarssl
CFLAGS += -DWANT_POLARSSL

# Comment in to enable gzip encoding
CFLAGS += -DWANT_ZLIB
ZDIR = zlib-1.2.8
CFLAGS += -DWANT_GZIP

# Currently I use gccgo
GO=$(shell which gccgo 2>/dev/null)
ifneq ($(GO),)
EXTRA+=go-get-comics
endif

CFILES  := http.c log.c openssl.c polarssl.c socket.c
COMMON	:= $(CFILES:.c=.o)

# Optionally add polarssl
ifneq ($(findstring WANT_POLARSSL,$(CFLAGS)),)
PLIB=polarssl/library/libpolarssl.a
CFLAGS += -DWANT_SSL -Ipolarssl/include
LLIBS += $(PLIB)
else
# Optionally add openssl
ifneq ($(findstring WANT_OPENSSL,$(CFLAGS)),)
CFLAGS += -DWANT_SSL
LIBS += -lssl -lcrypto
endif
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

LIBS += $(LLIBS)

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

get-comics: get-comics.o $(COMMON) config.o my-parser.o $(LLIBS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o get-comics $+ $(LIBS)
	@if [ -x /usr/bin/etags ]; then /usr/bin/etags *.c *.h; fi

link-check: link-check.o $(COMMON) $(LLIBS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o link-check $+ $(LIBS)

http-get: http-get.o $(COMMON) $(LLIBS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o http-get $+ $(LIBS)

go-get-comics: get-comics.go
	$(GO) -o $@ $+

$(PLIB):
	make -C polarssl -j4

$(ZLIB):
	make -C $(ZDIR) -j4

*.o: get-comics.h

get-comics.html: get-comics.1
	man2html get-comics.1 > get-comics.html

check:
	sparse get-comics.c $(CFILES) config.c my-parser.c

install: all
	install -D -s -m 755 get-comics $(DESTDIR)/usr/bin/get-comics
	install -D -m 644 get-comics.1 $(DESTDIR)/usr/man/man1/get-comics.1
	gzip $(DESTDIR)/usr/man/man1/get-comics.1
	install -D -m 644 comics.json $(DESTDIR)/usr/share/get-comics/comics.json

clean:
	rm -f get-comics link-check http-get *.o .*.o.d get-comics.html TAGS
	rm -f go-get-comics
	@make -C polarssl clean
	@make -C $(ZDIR) clean

include $(wildcard .*.o.d)
