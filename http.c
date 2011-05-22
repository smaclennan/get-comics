#include "get-comics.h"

#include <fcntl.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif


/*
 * Known limitations:
 *
 *   - assumes the reply header < buffersize
 *     - largest header I have seen < 500 bytes
 *   - does not handle compressed data
 *   - rudimentary https support
 */

#ifdef _WIN32
/* #define errno WSAGetLastError() */
#define EINPROGRESS WSAEWOULDBLOCK
#endif

static char *proxy;
static int proxy_port = 3128;

static char *http = "HTTP/1.1";


#define MIN(a, b)	((a) < (b) ? (a) : (b))

static int read_file(struct connection *conn);
static int read_file_unsized(struct connection *conn);
static int read_file_chunked(struct connection *conn);


void set_proxy(char *proxystr)
{
	char *p;

	if (proxy) {
		if (verbose)
			printf("WARNING: proxy set to %s:%d. Ignoring %s\n",
				   proxy, proxy_port, proxystr);
		return;
	}

	p = strrchr(proxystr, ':');
	if (p) {
		*p++ = '\0';
		proxy_port = strtol(p, NULL, 10);
	}

	proxy = must_strdup(proxystr);

	if (verbose)
		printf("Proxy %s:%d\n", proxy, proxy_port);
}


char *get_proxy(void)
{
	char *p;
	int len;

	if (!proxy)
		return NULL;

	len = strlen(proxy) + 16;

	p = malloc(len);
	if (p)
		sprintf(p, "%s:%d", proxy, proxy_port);

	return p;
}

static int tcp_connected(struct connection *conn)
{
	set_writable(conn);

#ifdef WANT_SSL
	if (is_https(conn->url)) {
		if (openssl_connect(conn)) {
			fail_connection(conn);
			return -1;
		}
	} else
#endif
		conn->connected = 1;

	return 0;
}

static int connect_socket(struct connection *conn, char *hostname, int port)
{
	struct sockaddr_in sock_name;
	int sock;
#ifdef _WIN32
	u_long optval;
#else
	int optval;
#endif
	struct hostent *host;

	host = gethostbyname(hostname);
	if (!host) {
		printf("Unable to get host %s\n", hostname);
		return -1;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		my_perror("socket");
		return -1;
	}

#ifdef _WIN32
	optval = 1;
	if (ioctlsocket(sock, FIONBIO, &optval)) {
		printf("ioctlsocket FIONBIO failed\n");
		close(sock);
		return -1;
	}
#else
	optval = fcntl(sock, F_GETFL, 0);
	if (optval == -1 || fcntl(sock, F_SETFL, optval | O_NONBLOCK)) {
		my_perror("fcntl O_NONBLOCK");
		close(sock);
		return -1;
	}
#endif

	if (!set_conn_socket(conn, sock)) {
		printf("Problems! Could not set socket\n");
		close(sock);
		return -1;
	}

	memset(&sock_name, 0, sizeof(sock_name));
	sock_name.sin_family = AF_INET;
	sock_name.sin_addr.s_addr = *(unsigned *)host->h_addr_list[0];
	sock_name.sin_port = htons((short)port);

	if (connect(sock, (struct sockaddr *)&sock_name, sizeof(sock_name))) {
		if (inprogress()) {
			if (verbose > 1)
				printf("Connection deferred\n");
			set_writable(conn);
			return 0;
		} else {
			char errstr[100];

			sprintf(errstr, "connect %.80s", hostname);
			my_perror(errstr);
			close(sock);
			return -1;
		}
	} else
		return tcp_connected(conn);
}


int build_request(struct connection *conn)
{
	char *url, *host, *p;

	conn->bufn = sizeof(conn->buf) - 1;

	url = is_http(conn->url);
	if (!url) {
#ifdef WANT_SSL
		printf("Only http/https supported\n");
#else
		printf("Only http supported\n");
#endif
		return 1;
	}

	p = strchr(url, '/');
	if (p) {
		*p = '\0';
		host = strdup(url);
		*p = '/';
		url = p;
	} else {
		host = strdup(url);
		url = "/";
	}

	if (!host) {
		printf("Out of memory\n");
		return 1;
	}

	if (proxy) {
		if (connect_socket(conn, proxy, proxy_port)) {
			printf("Connection failed to %s\n", host);
			free(host);
			return 1;
		}
		sprintf(conn->buf, "GET http://%s %s %s\r\n", host, url, http);
	} else {
		int port = is_https(conn->url) ? 443 : 80;

		p = strchr(host, ':');
		if (p) {
			/* port specified */
			*p++ = '\0';
			port = strtol(p, NULL, 10);
		}

		if (connect_socket(conn, host, port)) {
			printf("Connection failed to %s\n", host);
			free(host);
			return 1;
		}
		sprintf(conn->buf, "GET %s %s\r\nHost: %s\r\n",
			url, http, host);
	}

	free(host);

	if (verbose > 1)
		printf("%s %s", proxy ? ">P" : ">", conn->buf);

	if (conn->referer)
		sprintf(conn->buf + strlen(conn->buf),
			"Referer: %.200s\r\n", conn->referer);
	strcat(conn->buf, "\r\n");

	conn->curp = conn->buf;
	conn->length = strlen(conn->buf);

	return 0;
}


/* This should only be called if conn->connected == 0 */
void check_connect(struct connection *conn)
{
	int so_error;
	socklen_t optlen = sizeof(so_error);

#ifdef WANT_SSL
	if (conn->ssl) {
		/* Wait for openssl connection to connect */
		if (openssl_check_connect(conn))
			fail_connection(conn);
		return;
	}
#endif

	if (getsockopt(conn->poll->fd, SOL_SOCKET, SO_ERROR,
		       (char *)&so_error, &optlen) == 0 &&
	    so_error == 0)
		tcp_connected(conn);
}


void write_request(struct connection *conn)
{
	size_t n;

#ifdef WANT_SSL
	if (conn->ssl) {
		n = openssl_write(conn);
		/* openssl_write can return -EAGAIN if the SSL
		 * connection needs a read or write. */
		if (n == -EAGAIN)
			return;
	} else
#endif
		n = write(conn->poll->fd, conn->curp, conn->length);

	if (n == conn->length) {
		if (verbose > 2)
			printf("+ Sent request\n");
		conn->length = 0;

		/* reset for read */
		set_readable(conn);
		conn->curp = conn->buf;
		conn->rlen = conn->bufn;
		NEXT_STATE(conn, read_reply);
	} else if (n > 0) {
		conn->length -= n;
		conn->curp += n;
	} else {
		printf("Write request error\n");
		fail_connection(conn);
	}
}


/* State function */
int read_reply(struct connection *conn)
{
	char *p, *fname;
	int status = 1;
	int chunked = 0;

	p = strstr(conn->buf, "\n\r\n");
	if (p) {
		*(p + 1) = '\0';
		conn->curp = p + 3;
		if (verbose > 1)
			printf("- Reply %d bytes\n", conn->curp - conn->buf);
	} else if (conn->curp == conn->endp) {
		printf("Unexpected EOF %s\n", conn->url);
		return 1;
	} else if (conn->rlen > 0) {
		/* I have never seen this happen */
		conn->curp = conn->endp;
		return 0;
	} else {
		/* I have never seen this happen */
		printf("REPLY TOO LONG %s\n", conn->url);
		return 1;
	}

	if (verbose > 2)
		fputs(conn->buf, stdout);

	if (strncmp(conn->buf, "HTTP/1.1 ", 9) &&
	    strncmp(conn->buf, "HTTP/1.0 ", 9)) {
		if (verbose)
			printf("%s: Bad status line %s\n",
			       conn->host, conn->buf);
		return 1;
	}

	status = strtol(conn->buf + 9, NULL, 10);

	switch (status) {
	case 200: /* OK */
		if (verbose)
			printf("200 %s\n", conn->url);

		p = strstr(conn->buf, "Content-Length:");
		if (!p)
			p = strstr(conn->buf, "Content-length:");
		conn->length = p ? strtol(p + 15, NULL, 10) : 0;

		p = strstr(conn->buf, "Transfer-Encoding:");
		if (p) {
			p += 18;
			while (isspace(*p))
				++p;
			if (strncmp(p, "chunk", 5) == 0) {
				if (verbose > 1)
					printf("Chunking\n");
				chunked = 1;
			} else
				printf("OH oh. %s: %s", conn->host, p);
		}
		if (verbose > 1 && conn->length == 0 && !chunked)
			printf("Warning: No content length for %s\n",
			       conn->url);
		break;

	case 301: /* Moved Permanently */
	case 302: /* Moved Temporarily */
		p = strstr(conn->buf, "Location:");
		if (p) {
			char *e;

			for (p += 9; isspace(*p); ++p)
				;
			e = strchr(p, '\n');
			if (e) {
				while (isspace(*(e - 1)))
					--e;
				*e = '\0';
				printf("WARNING: %s redirected to %s\n",
				       conn->url, p);
				release_connection(conn);
				free(conn->url);
				conn->url = strdup(p);
				if (!conn->url) {
					printf("Out of memory\n");
					return 1;
				}

				/* This will cause a bogus Multiple
				 * Closes error if it fails. */
				if (build_request(conn))
					return fail_redirect(conn);
			}
		}
		printf("%s: %d with no new location\n", conn->host, status);
		return status;

	case 0:
		printf("HUH? NO STATUS\n");
		status = 2;
		/* fall thru */
	default:
		printf("%d: %s\n", status, conn->url);
		return status;
	}

	fname = conn->outname;
	if (conn->regexp && !conn->matched)
		fname = conn->regfname;
	else if (fname == NULL) {
		/* User did not supply a filename. Get it from the URL. */
		p = strrchr(conn->url, '/');
		if (p) {
			++p;
			if (*p)
				fname = p;
			else
				fname = "index.html";
		} else
			fname = conn->url;
	}

	conn->out = fopen(fname, "wb");
	if (!conn->out) {
		my_perror(fname);
		return 1;
	}

	if (verbose > 1)
		printf("Output %s -> %s\n", conn->url, fname);

	if (chunked) {
		conn->cstate = CS_DIGITS;
		NEXT_STATE(conn, read_file_chunked);
		conn->length = 0; /* paranoia */
	} else if (conn->length == 0)
		NEXT_STATE(conn, read_file_unsized);
	else
		NEXT_STATE(conn, read_file);

	if (conn->curp < conn->endp)
		return conn->func(conn);

	return 0;
}


/* State function */
static int read_chunkblock(struct connection *conn)
{
	size_t bytes;

	bytes = conn->endp - conn->curp;
	if (bytes > (size_t)conn->length)
		bytes = conn->length;

	if (bytes > 0) {
		if (fwrite(conn->curp, 1, bytes, conn->out) != bytes) {
			printf("Write error\n");
			return 1;
		}
	}

	if (bytes >= 0) {
		conn->length -= bytes;
		if (conn->length <= 0) {
			if (verbose > 1)
				printf("Read block\n");
			conn->curp += bytes;
			conn->length = 0;
			conn->cstate = CS_START_CR;
			NEXT_STATE(conn, read_file_chunked);
			if (conn->endp > conn->curp)
				return read_file_chunked(conn);
			return 0;
		}
	} else {
		printf("Read chunk file problems %d for %s\n",
		       bytes, conn->url);
		return 1;
	}

	conn->curp = conn->buf;
	conn->rlen = conn->bufn;
	return 0;
}


/* Make code more readable */
#define INC_CURP(conn)						\
	do {							\
		if (++conn->curp == conn->endp) {		\
			if (verbose > 1)			\
				printf("Empty %d\n", __LINE__);	\
			return 0;				\
		}						\
	} while (0)


/* State function */
static int read_file_chunked(struct connection *conn)
{
	if (conn->curp >= conn->endp) {
		if (verbose > 1)
			printf("Hmmm, already empty\n");
		return 1;
	}

	if (conn->cstate == CS_START_CR) {
		if (*conn->curp != '\r') {
			printf("BAD CHUNK END '%02x'\n", *conn->curp);
			return 1;
		}
		conn->cstate = CS_START_LF;
		INC_CURP(conn);
	}

	if (conn->cstate == CS_START_LF) {
		if (*conn->curp != '\n') {
			printf("BAD CHUNK END '%02x'\n", *conn->curp);
			return 1;
		}
		conn->cstate = CS_DIGITS;
		INC_CURP(conn);
	}

	if (conn->cstate == CS_DIGITS) {
		while (isxdigit(*conn->curp)) {
			if (isdigit(*conn->curp))
				conn->length = conn->length * 16 +
					*conn->curp - '0';
			else
				conn->length = conn->length * 16 +
					tolower(*conn->curp) - 'a' + 10;
			INC_CURP(conn);
		}
		conn->cstate = CS_END_CR;
	}

	if (conn->cstate == CS_END_CR) {
		/* Apache seems to tack on spaces (for last real block?) */
		while (*conn->curp == ' ')
			INC_CURP(conn);

		if (*conn->curp != '\r') {
			printf("BAD CHUNK LINE '%02x'\n", *conn->curp);
			return 1;
		}
		conn->cstate = CS_END_LF;
		INC_CURP(conn);
	}

	if (*conn->curp != '\n') {
		printf("BAD CHUNK LINE '%02x'\n", *conn->curp);
		return 1;
	}
	++conn->curp; /* not INC_CURP */

	if (conn->length > 0) {
		if (verbose > 2)
			printf("Chunk %x = %d\n", conn->length, conn->length);
		NEXT_STATE(conn, read_chunkblock);
		return read_chunkblock(conn);
	}

	if (verbose > 1)
		printf("Last chunk\n");
	conn->cstate = CS_NONE;
	if (conn->regexp && !conn->matched)
		return process_html(conn);
	close_connection(conn);
	return 0;
}


/* State function */
static int read_file_unsized(struct connection *conn)
{
	size_t bytes;

	bytes = conn->endp - conn->curp;
	if (bytes > 0) {
		if (fwrite(conn->curp, 1, bytes, conn->out) != bytes) {
			printf("Write error\n");
			return 1;
		}
	} else {
		if (verbose)
			printf("OK %s\n", conn->url);
		if (conn->regexp && !conn->matched)
			return process_html(conn);
		close_connection(conn);
		return 0;
	}

	conn->curp = conn->buf;
	conn->rlen = conn->bufn;
	return 0;
}


/* State function */
static int read_file(struct connection *conn)
{
	size_t bytes;

	bytes = conn->endp - conn->curp;
	if (bytes > 0) {
		if (fwrite(conn->curp, 1, bytes, conn->out) != bytes) {
			printf("Write error\n");
			return 1;
		}
		conn->length -= bytes;
		if (conn->length <= 0) {
			if (verbose)
				printf("OK %s\n", conn->url);
			if (conn->regexp && !conn->matched)
				return process_html(conn);
			close_connection(conn);
			return 0;
		}
	} else {
		printf("Read file problems %d for %s!\n",
		       bytes, conn->url);
		return 1;
	}

	conn->curp = conn->buf;
	conn->rlen = conn->bufn;
	return 0;
}
