# SAM CFLAGS += -O3 -Wall -g
CFLAGS += -Wall -g

# For dependencies
CFLAGS += -Wp,-MD,$(@D)/.$(@F).d

# Comment in to enable the log_* functions
CFLAGS += -DLOGGING

# Comment in to enable https via openssl
CFLAGS += -DWANT_SSL

# Comment in to enable gzip encoding
CFLAGS += -DWANT_GZIP

# Currently I use gccgo
GO=$(shell which gccgo 2>/dev/null)
ifneq ($(GO),)
EXTRA+=go-get-comics
endif

OBJS    := get-comics.o http.o log.o openssl.o socket.o config.o JSON_parser.o
LC_OBJS := link-check.o http.o log.o openssl.o socket.o
HG_OBJS := http-get.o http.o log.o openssl.o socket.o

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

all:	get-comics link-check http-get $(EXTRA) TAGS

get-comics: $(OBJS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o get-comics $(OBJS) $(LIBS)

link-check: $(LC_OBJS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o link-check $(LC_OBJS) $(LIBS)

http-get: $(HG_OBJS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o http-get $(HG_OBJS) $(LIBS)

go-get-comics: get-comics.go
	$(GO) -o $@ $+

TAGS: $(OBJS) $(LC_OBJS)
	@if [ -x /usr/bin/etags ]; then /usr/bin/etags *.c *.h; else touch TAGS; fi

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

DEP_FILES := $(wildcard .*.o.d)
$(if $(DEP_FILES),$(eval include $(DEP_FILES)))
