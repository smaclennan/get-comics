CFLAGS += -O3 -Wall -g

# Comment in to enable the log_* functions
CFLAGS += -DLOGGING

# Comment in to enable https via openssl
CFLAGS += -DWANT_SSL

# Comment in to enable XML
CFLAGS += -DWANT_XML

# Comment in only one to enable JSON
JSON := WANT_JSON_LIB
#JSON := WANT_JSON_INTERNAL
#JSON := WANT_JSON_PARSER

OBJS := get-comics.o http.o config.o log.o openssl.o

# For libxml2
ifneq ($(findstring WANT_XML,$(CFLAGS)),)
OBJS += xml.o
CFLAGS += -I/usr/include/libxml2
LIBS += -lxml2
endif

# For json
ifneq ($(findstring WANT_JSON_INTERNAL,$(JSON)),)
CFLAGS += -DWANT_JSON
OBJS += json-internal.o js0n.o
endif
ifneq ($(findstring WANT_JSON_PARSER,$(JSON)),)
CFLAGS += -DWANT_JSON
OBJS += json-parser.o JSON_parser.o
endif
ifneq ($(findstring WANT_JSON_LIB,$(JSON)),)
CFLAGS += -DWANT_JSON
OBJS += json.o
LIBS += -ljson
endif

# Optionaly add openssl
ifneq ($(findstring WANT_SSL,$(CFLAGS)),)
LIBS += -lssl
endif

all: 	get-comics get-comics.html comics.xml

get-comics: $(OBJS)
	$(CC) $(CFLAGS) -o get-comics $(OBJS) $(LIBS)

*.o: get-comics.h

get-comics.html: get-comics.1
	man2html get-comics.1 > get-comics.html

comics.xml: comics.json
	$(CC) $(CFLAGS) -o json2xml json2xml.c
	./json2xml < comics.json > comics.xml

install:
	install -D -s -m 755 get-comics $(DESTDIR)/usr/bin/get-comics
	install -D -m 644 get-comics.1 $(DESTDIR)/usr/man/man1/get-comics.1
	gzip $(DESTDIR)/usr/man/man1/get-comics.1
	install -D -m 644 comics.json $(DESTDIR)/usr/share/get-comics/comics.json
	install -D -m 644 comics.xml $(DESTDIR)/usr/share/get-comics/comics.xml

clean:
	rm -f get-comics json2xml *.o get-comics.html comics.xml TAGS
