CFLAGS += -O3 -Wall -g -Wno-unused-result

# For dependencies
CFLAGS += -Wp,-MD,$(@D)/.$(@F).d

# Comment in to enable the log_* functions
CFLAGS += -DLOGGING

# Comment in to enable https via openssl
CFLAGS += -DWANT_SSL

OBJS := get-comics.o http.o config.o log.o openssl.o JSON_parser.o

# Optionaly add openssl
ifneq ($(findstring WANT_SSL,$(CFLAGS)),)
LIBS += -lssl
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

all: 	get-comics

get-comics: $(OBJS)
	$(QUIET_LINK)$(CC) $(CFLAGS) -o get-comics $(OBJS) $(LIBS)

*.o: get-comics.h

get-comics.html: get-comics.1
	man2html get-comics.1 > get-comics.html

install:
	install -D -s -m 755 get-comics $(DESTDIR)/usr/bin/get-comics
	install -D -m 644 get-comics.1 $(DESTDIR)/usr/man/man1/get-comics.1
	gzip $(DESTDIR)/usr/man/man1/get-comics.1
	install -D -m 644 comics.json $(DESTDIR)/usr/share/get-comics/comics.json

clean:
	rm -f get-comics *.o .*.o.d get-comics.html TAGS

DEP_FILES := $(wildcard .*.o.d)
$(if $(DEP_FILES),$(eval include $(DEP_FILES)))
