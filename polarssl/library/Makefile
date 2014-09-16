
# Also see "include/polarssl/config.h"

CFLAGS	+= -I../include -D_FILE_OFFSET_BITS=64 -Wall -W -Wdeclaration-after-statement
OFLAGS	= -O2

ifdef DEBUG
CFLAGS += -g3
endif

OBJS=	aes.o aesni.o asn1parse.o base64.o bignum.o certs.o cipher.o \
	cipher_wrap.o ctr_drbg.o debug.o dhm.o entropy.o entropy_poll.o	\
	error.o md.o md_wrap.o md5.o oid.o pem.o pk.o pk_wrap.o pkparse.o \
	rsa.o sha1.o sha256.o sha512.o ssl_cli.o ssl_ciphersuites.o ssl_tls.o \
	version.o x509.o x509_crt.o

.SILENT:

all: libpolarssl.a

libpolarssl.a: $(OBJS)
	echo "     AR       $@"
	$(AR) cr $@ $(OBJS)
	$(AR) s $@

.c.o:
	echo "     CC       $<"
	$(CC) $(CFLAGS) $(OFLAGS) -c $<

clean:
	rm -f *.o libpolarssl.*
