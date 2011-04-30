CFLAGS += -O3 -Wall -g

# Comment in to enable the log_* functions
CFLAGS += -DLOGGING

# Comment in to enable https via openssl
CFLAGS += -DWANT_SSL

# Comment in to enable XML
CFLAGS += -DWANT_XML

# Comment in to enable JSON
#CFLAGS += -DWANT_JSON

OBJS := get-comics.o http.o log.o openssl.o

# For libxml2
ifneq ($(findstring WANT_XML,$(CFLAGS)),)
OBJS += xml.o
CFLAGS += -I/usr/include/libxml2
LIBS += -lxml2
endif

# For json-c
ifneq ($(findstring WANT_JSON,$(CFLAGS)),)
OBJS += json.o
LIBS += -ljson
endif

# Optionaly add openssl
ifneq ($(findstring WANT_SSL,$(CFLAGS)),)
LIBS += -lssl
endif

all: 	get-comics get-comics.html

get-comics: $(OBJS)
	$(CC) $(CFLAGS) -o get-comics $(OBJS) $(LIBS)

*.o: get-comics.h

get-comics.html: get-comics.1
	man2html get-comics.1 > get-comics.html

install:
	install -D -s -m 755 get-comics $(DESTDIR)/usr/bin/get-comics
	install -D -m 644 get-comics.1 $(DESTDIR)/usr/man/man1/get-comics.1
	gzip $(DESTDIR)/usr/man/man1/get-comics.1
	install -D -m 644 comics.xml $(DESTDIR)/usr/share/get-comics/comics.xml

clean:
	rm -f get-comics http *.o get-comics.html TAGS
