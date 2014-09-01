
# Also see "include/polarssl/config.h"

# To compile on MinGW: add "-lws2_32" to LDFLAGS or define WINDOWS in your
# environment
#
CFLAGS	+= -I../include -D_FILE_OFFSET_BITS=64 -Wall -W -Wdeclaration-after-statement
OFLAGS	= -O2

ifdef DEBUG
CFLAGS += -g3
endif

# MicroBlaze specific options:
# CFLAGS += -mno-xl-soft-mul -mxl-barrel-shift

# To compile on Plan9:
# CFLAGS += -D_BSD_EXTENSION

# To compile as a shared library:
ifdef SHARED
CFLAGS += -fPIC
endif

SONAME=libpolarssl.so.5

DLEXT=so.5
# OSX shared library extension:
# DLEXT=dylib

# Windows shared library extension:
ifdef WINDOWS
DLEXT=dll
LDFLAGS += -lws2_32
endif

OBJS=	aes.o		aesni.o					\
		asn1parse.o								\
		base64.o	bignum.o		\
		certs.o		cipher.o	cipher_wrap.o	\
		ctr_drbg.o	debug.o					\
		dhm.o							\
		entropy.o	entropy_poll.o				\
		error.o				\
		md.o		md_wrap.o				\
				md5.o				\
		oid.o									\
			pbkdf2.o	pem.o			\
		pk.o		pk_wrap.o	pkparse.o		\
		rsa.o		sha1.o		sha256.o		\
		sha512.o	ssl_cache.o	ssl_cli.o		\
		   ssl_ciphersuites.o			\
		ssl_tls.o	timing.o		\
		version.o								\
		x509.o		x509_crt.o			\

.SILENT:

ifndef SHARED
all: static
else
all: shared
endif

static: libpolarssl.a

shared: libpolarssl.$(DLEXT) libpolarssl.so

libpolarssl.a: $(OBJS)
	echo "  AR    $@"
	$(AR) r $@ $(OBJS)
	echo "  RL    $@"
	$(AR) s $@

libpolarssl.${DLEXT}: libpolarssl.a
	echo "  LD    $@"
	$(CC) ${LDFLAGS} -shared -Wl,-soname,$(SONAME) -o $@ $(OBJS)

libpolarssl.so: libpolarssl.${DLEXT}
	echo "  LN    $@ -> libpolarssl.${DLEXT}"
	ln -sf libpolarssl.${DLEXT} $@

libpolarssl.dylib: libpolarssl.a
	echo "  LD    $@"
	$(CC) ${LDFLAGS} -dynamiclib -o $@ $(OBJS)

libpolarssl.dll: libpolarssl.a
	echo "  LD    $@"
	$(CC) -shared -Wl,-soname,$@ -o $@ $(OBJS) -lws2_32 -lwinmm -lgdi32

.c.o:
	echo "  CC    $<"
	$(CC) $(CFLAGS) $(OFLAGS) -c $<

clean:
ifndef WINDOWS
	rm -f *.o libpolarssl.*
endif
ifdef WINDOWS
	del /Q /F *.o libpolarssl.*
endif
