#include "get-comics.h"

#ifdef WANT_MBEDTLS
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/net.h"

static int initialized;
static mbedtls_ssl_config config;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;

int openssl_check_connect(struct connection *conn)
{
	int rc = mbedtls_ssl_handshake(conn->ssl);
	switch (rc) {
	case 0: /* success */
		conn->connected = 1;
		set_writable(conn);
		if (verbose)
			printf("Ciphersuite is %s\n",
				   mbedtls_ssl_get_ciphersuite(conn->ssl));
		return 0;
	case MBEDTLS_ERR_SSL_WANT_READ:
		set_readable(conn);
		return 0;
	case MBEDTLS_ERR_SSL_WANT_WRITE:
		set_writable(conn);
		return 0;
	case MBEDTLS_ERR_NET_CONN_RESET:
		reset_connection(conn);
		return 1;
	default:
		printf("Not read or write 0x%x\n", rc);
		return 1;
	}
}

/* Returns an opaque ssl context */
int openssl_connect(struct connection *conn)
{
	if (!initialized) {
		initialized = 1;

		mbedtls_entropy_init(&entropy);
		mbedtls_ctr_drbg_init(&ctr_drbg);

		mbedtls_ssl_config_init(&config);
		if (mbedtls_ssl_config_defaults(&config,
										MBEDTLS_SSL_IS_CLIENT,
										MBEDTLS_SSL_TRANSPORT_STREAM,
										MBEDTLS_SSL_PRESET_DEFAULT)) {
			printf("Unable to initialize ssl defaults\n");
			exit(1);
		}
	}

	mbedtls_ssl_context *ssl = malloc(sizeof(mbedtls_ssl_context));
	if (!ssl) {
		printf("Out of memory");
		return 1;
	}

	mbedtls_ssl_init(ssl);
	if (mbedtls_ssl_setup(ssl, &config)) {
		printf("Unable to set ssl defaults\n");
		exit(1);
	}

	conn->ssl = ssl;


//	mbedtls_ssl_set_rng(ssl, ctr_drbg_random, &ctr_drbg);

	int *fd = &conn->poll->fd;
	mbedtls_ssl_set_bio(ssl, fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	return openssl_check_connect(conn);
}

int openssl_read(struct connection *conn)
{
	int n = mbedtls_ssl_read(conn->ssl, (unsigned char *)conn->curp, conn->rlen);
	if (n < 0)
		switch (n) {
		case MBEDTLS_ERR_SSL_WANT_READ:
		case MBEDTLS_ERR_SSL_WANT_WRITE:
			return -EAGAIN;
		default:
			printf("Not read or write\n");
			break;
		}

	return n;
}

int openssl_write(struct connection *conn)
{
	int n = mbedtls_ssl_write(conn->ssl, (unsigned char *)conn->curp, conn->length);
	if (n < 0)
		switch (n) {
		case MBEDTLS_ERR_SSL_WANT_READ:
		case MBEDTLS_ERR_SSL_WANT_WRITE:
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
		mbedtls_ssl_close_notify(conn->ssl);
		mbedtls_ssl_free(conn->ssl);
		free(conn->ssl);
		conn->ssl = NULL;
	}
}

void openssl_list_ciphers(void)
{
	const int *list = mbedtls_ssl_list_ciphersuites();
	while (*list) {
		printf(" %-42s", mbedtls_ssl_get_ciphersuite_name( *list ) );
		list++;
		if( !*list )
		break;
		printf(" %s\n", mbedtls_ssl_get_ciphersuite_name( *list ) );
	    list++;
	}
}
#endif
