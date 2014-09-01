#include "get-comics.h"

#ifdef WANT_POLARSSL
#include "polarssl/ssl.h"
#include "polarssl/entropy.h"
#include "polarssl/ctr_drbg.h"
#include "polarssl/error.h"

static int initialized;
static entropy_context entropy;
static ctr_drbg_context ctr_drbg;

int openssl_check_connect(struct connection *conn)
{
	int rc = ssl_handshake(conn->ssl);
	switch (rc) {
	case 0: /* success */
		conn->connected = 1;
		set_writable(conn);
		if (verbose)
			printf("Ciphersuite is %s\n",
			       ssl_get_ciphersuite(conn->ssl));
		return 0;
	case POLARSSL_ERR_NET_WANT_READ:
		set_readable(conn);
		return 0;
	case POLARSSL_ERR_NET_WANT_WRITE:
		set_writable(conn);
		return 0;
	default:
		printf("Not read or write %d\n", rc);
		return 1;
	}
}

static int netrecv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    int n = read(fd, buf, len);
    if (n < 0) {
	    if (errno == EWOULDBLOCK || errno == EINTR)
		    return POLARSSL_ERR_NET_WANT_READ;
	    else
		    return POLARSSL_ERR_NET_RECV_FAILED;
    }
    return n;
}

static int netsend(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *) ctx;
    int n = write(fd, buf, len);
    if (n < 0) {
	    if (errno == EWOULDBLOCK || errno == EINTR)
		    return POLARSSL_ERR_NET_WANT_WRITE;
	    else
		    return POLARSSL_ERR_NET_SEND_FAILED;
    }
    return n;
}

/* Returns an opaque ssl context */
int openssl_connect(struct connection *conn)
{
	if (!initialized) {
		initialized = 1;

		entropy_init(&entropy);
		if (ctr_drbg_init(&ctr_drbg, entropy_func, &entropy,
				  (uint8_t *)"get-comics", 10)) {
			puts("ctr_drbg_init failed! We need more entropy.");
			exit(1);
		}
	}

	ssl_context *ssl = malloc(sizeof(ssl_context));
	if (!ssl) {
		printf("Out of memory");
		return 1;
	}

	int rc = ssl_init(ssl);
	if (rc) {
		char error_buf[100];

		puts("ssl_init failed");
		polarssl_strerror(rc, error_buf, sizeof(error_buf));
		printf("Polarssl: %d: %s\n", rc, error_buf);
		return 1;
	}

	conn->ssl = ssl;

	ssl_set_endpoint(ssl, SSL_IS_CLIENT);
	ssl_set_authmode(ssl, SSL_VERIFY_NONE);

	ssl_set_rng(ssl, ctr_drbg_random, &ctr_drbg);

	int *fd = &conn->poll->fd;
	ssl_set_bio(ssl, netrecv, fd, netsend, fd);

	return openssl_check_connect(conn);
}

int openssl_read(struct connection *conn)
{
	int n = ssl_read(conn->ssl, (uint8_t *)conn->curp, conn->rlen);
	if (n < 0)
		switch (n) {
		case POLARSSL_ERR_NET_WANT_READ:
		case POLARSSL_ERR_NET_WANT_WRITE:
			return -EAGAIN;
		default:
			printf("Not read or write\n");
			break;
		}

	return n;
}

int openssl_write(struct connection *conn)
{
	int n = ssl_write(conn->ssl, (uint8_t *)conn->curp, conn->length);
	if (n < 0)
		switch (n) {
		case POLARSSL_ERR_NET_WANT_READ:
		case POLARSSL_ERR_NET_WANT_WRITE:
			return -EAGAIN;
		default:
			printf("Not read or write\n");
			break;
		}

	return n;
}

void openssl_close(struct connection *conn)
{
	if (conn->ssl) {
		ssl_close_notify(conn->ssl);
		ssl_free(conn->ssl);
		free(conn->ssl);
		conn->ssl = NULL;
	}
}

void openssl_list_ciphers(void)
{
	const int *list = ssl_list_ciphersuites();
	while (*list) {
	    printf(" %-42s", ssl_get_ciphersuite_name( *list ) );
	    list++;
	    if( !*list )
		break;
	    printf(" %s\n", ssl_get_ciphersuite_name( *list ) );
	    list++;
	}
}
#else
void openssl_close(struct connection *conn) {}
#endif
