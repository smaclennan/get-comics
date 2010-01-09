CFLAGS += -O3 -Wall -g

OBJS := get-comics.o http.o xml.o

# For libxml2
CFLAGS += -I/usr/include/libxml2
LIBS += -lxml2

all: 	get-comics get-comics.html

get-comics: $(OBJS)
	$(CC) $(CFLAGS) -o get-comics $(OBJS) $(LIBS)

http: http.c
	$(CC) $(CFLAGS) -DSTANDALONE -o http http.c

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
