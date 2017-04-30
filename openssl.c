#include "get-comics.h"

#ifdef WANT_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

/* Application-wide SSL context. This is common to all SSL
 * connections.  */
static SSL_CTX *ssl_ctx;

static void print_errors(void)
{
	unsigned long err;
	while ((err = ERR_get_error()))
		printf("OpenSSL: %s\n", ERR_error_string(err, NULL));
}

static int openssl_init(void)
{
	if (ssl_ctx)
		return 0;

	if (RAND_status() != 1) {
		printf("Could not seed PRNG\n");
		return -ENOENT;
	}

	SSL_library_init();
	SSL_load_error_strings();
	SSLeay_add_all_algorithms();
	SSLeay_add_ssl_algorithms();

	ssl_ctx = SSL_CTX_new(SSLv23_client_method());
	if (!ssl_ctx) {
		print_errors();
		return 1;
	}

	/* SSL_VERIFY_NONE instructs OpenSSL not to abort SSL_connect
	 * if the certificate is invalid. */
	SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);

	SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);

	SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);

	return 0;
}

int openssl_check_connect(struct connection *conn)
{
	int ret = SSL_connect(conn->ssl);
	if (ret <= 0)
		switch (SSL_get_error(conn->ssl, ret)) {
		case SSL_ERROR_WANT_READ:
			set_readable(conn);
			return 0;
		case SSL_ERROR_WANT_WRITE:
			set_writable(conn);
			return 0;
		default:
			printf("Not read or write\n");
			return 1;
		}

	conn->connected = 1;

	set_writable(conn);

	return 0;
}

/* Returns an opaque ssl context */
int openssl_connect(struct connection *conn)
{
	SSL *ssl;

	if (openssl_init())
		return 1;

	ssl = SSL_new(ssl_ctx);
	if (!ssl)
		goto error;

	conn->ssl = ssl;

	if (!SSL_set_fd(ssl, conn->poll->fd))
		goto error;

	SSL_set_connect_state(ssl);

	return openssl_check_connect(conn);

error:
	printf("SSL handshake failed.\n");
	print_errors();
	return 1;
}

int openssl_read(struct connection *conn)
{
	int n, err;

	do
		n = SSL_read(conn->ssl, conn->curp, conn->rlen);
	while (n < 0 &&
	       (err = SSL_get_error(conn->ssl, n)) == SSL_ERROR_SYSCALL &&
	       errno == EINTR);

	if (n < 0)
		switch (err) {
		case SSL_ERROR_WANT_READ:
			return -EAGAIN;
		case SSL_ERROR_WANT_WRITE:
			return -EAGAIN;
		default:
			printf("Not read or write\n");
			break;
		}

	return n;
}

int openssl_write(struct connection *conn)
{
	int n, err;

	do
		n = SSL_write(conn->ssl, conn->curp, conn->length);
	while (n < 0 &&
	       (err = SSL_get_error(conn->ssl, n)) == SSL_ERROR_SYSCALL &&
	       errno == EINTR);

	if (n < 0)
		switch (err) {
		case SSL_ERROR_WANT_READ:
			return -EAGAIN;
		case SSL_ERROR_WANT_WRITE:
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
		SSL_shutdown(conn->ssl);
		SSL_free(conn->ssl);
		conn->ssl = NULL;
	}
}

void openssl_list_ciphers(void)
{
	FILE *pfp = popen("openssl ciphers", "r");
	if (!pfp) {
		my_perror("openssl ciphers");
		return;
	}

	int count = 0, ccount = 0, ch, i;
	while ((ch = fgetc(pfp)) != EOF)
		if (ch == ':') {
			if (count == 0)
				for (i = ccount; i < 42; ++i)
					putchar(' ');
			else putchar('\n');
			count = !count;
			ccount = 0;
		} else {
			putchar(ch);
			++ccount;
		}

	pclose(pfp);
}
#endif

