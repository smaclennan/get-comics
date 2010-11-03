#include "get-comics.h"

#ifdef WANT_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <fcntl.h> /* SAM FIXME */

/* Application-wide SSL context. This is common to all SSL
 * connections.  */
static SSL_CTX *ssl_ctx;

static void print_errors (void)
{
	unsigned long err;
	while ((err = ERR_get_error ()))
		printf ("OpenSSL: %s\n", ERR_error_string(err, NULL));
}

static int openssl_init()
{
	if (ssl_ctx)
		return 0;

	if (RAND_status() != 1) {
		printf ("Could not seed PRNG\n");
		return ENOENT;
	}

	SSL_library_init ();
	SSL_load_error_strings ();
	SSLeay_add_all_algorithms ();
	SSLeay_add_ssl_algorithms ();

	ssl_ctx = SSL_CTX_new(SSLv23_client_method());
	if (!ssl_ctx) {
		print_errors();
		return 1;
	}

	/* SSL_VERIFY_NONE instructs OpenSSL not to abort SSL_connect
	 * if the certificate is invalid. */
	SSL_CTX_set_verify (ssl_ctx, SSL_VERIFY_NONE, NULL);

	SSL_CTX_set_mode (ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);

	SSL_CTX_set_mode (ssl_ctx, SSL_MODE_AUTO_RETRY);

	return 0;
}

/* Returns an opaque ssl context */
int openssl_connect(struct connection *conn)
{
	SSL *ssl;
	int optval;

	if (openssl_init())
		return 1;

	/* SSL connection must be blocking */
	optval = fcntl(conn->sock, F_GETFL, 0);
	if (optval == -1 || fcntl(conn->sock, F_SETFL, optval & ~O_NONBLOCK)) {
		my_perror("fcntl O_NONBLOCK");
		return 1;
	}

	ssl = SSL_new(ssl_ctx);
	if (!ssl)
		goto error;

	if (!SSL_set_fd(ssl, conn->sock))
		goto error;
	SSL_set_connect_state(ssl);
	if (SSL_connect(ssl) <= 0 || ssl->state != SSL_ST_OK)
		goto error;

	conn->ssl = ssl;

	return 0;

error:
	printf ("SSL handshake failed.\n");
	print_errors();
	if (ssl)
		SSL_free(ssl);
	return 1;
}

int openssl_read(struct connection *conn)
{
	int n;

	do
		n = SSL_read(conn->ssl, conn->curp, conn->rlen);
	while (n == -1 &&
	       SSL_get_error(conn->ssl, n) == SSL_ERROR_SYSCALL &&
	       errno == EINTR);

	return n;
}

int openssl_write(struct connection *conn)
{
	int n;

	do
		n = SSL_write(conn->ssl, conn->curp, conn->length);
	while (n == -1 &&
	       SSL_get_error(conn->ssl, n) == SSL_ERROR_SYSCALL &&
	       errno == EINTR);

	return n;
}

void openssl_close(struct connection *conn)
{
	if (conn->ssl) {
		SSL_shutdown(conn->ssl);
		SSL_free(conn->ssl);
		conn->ssl = NULL;
	}
}
#else
void openssl_close(struct connection *conn) {}
#endif

