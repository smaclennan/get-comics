#include "get-comics.h"

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
#undef EINPROGRESS /* we need the windows socket define */
#define EINPROGRESS WSAEWOULDBLOCK
/* Windows doesn't support this. */
#define MSG_NOSIGNAL 0
#endif

static char *http = "HTTP/1.1";

#define MIN(a, b)	((a) < (b) ? (a) : (b))

static int read_file(struct connection *conn);
static int read_file_unsized(struct connection *conn);
static int read_file_chunked(struct connection *conn);
static int gzip_init(struct connection *conn);
static int read_file_gzip(struct connection *conn);
static int write_output_gzipped(struct connection *conn, size_t bytes);

static inline void reset_buf(struct connection *conn)
{
	conn->curp = conn->buf;
	conn->rlen = BUFSIZE;
}

/* This is only for 2 stage comics and redirects */
int release_connection(struct connection *conn)
{
	if (verbose > 2)
		printf("Release %s\n", conn->url);

#ifdef WANT_SSL
	openssl_close(conn);
#endif

	if (conn->poll && conn->poll->fd != -1) {
		closesocket(conn->poll->fd);
		conn->poll->fd = -1;
	}
	conn->poll = NULL;

	if (conn->out >= 0) {
		close(conn->out);
		conn->out = -1;
	}

	conn->func = NULL;

	conn->connected = 0;

	if (conn->zs) {
		inflateEnd(conn->zs);
		free(conn->zs);
		conn->zs = NULL;
		if (conn->zs_buf)
			free(conn->zs_buf);
	}

	return 0;
}


/* Fail a redirect. We have already released the connection. */
static int fail_redirect(struct connection *conn)
{
	if (conn->poll)
		printf("Failed redirect not closed: %s\n", conn->url);
	else {
		--outstanding;
		if (verbose > 1)
			printf("Failed redirect %s (%d)\n",
			       conn->url, outstanding);
	}
	log_clear(conn);
	return 0;
}

static void add_full_header(struct connection *conn, const char *host)
{
	int n = strlen(conn->buf);

	/* Always need host */
	n += snprintf(conn->buf + n, BUFSIZE - n, "Host: %s\r\n", host);

	/* Header strings taken from Firefox. ~300 bytes.
	 * Some comics (e.g. sinfest) require the user agent.
	 */
	n += snprintf(conn->buf + n, BUFSIZE - n, "User-Agent: %s\r\n", user_agent);

//#define FULL_HEADER
#ifdef FULL_HEADER
	n += snprintf(conn->buf + n, BUFSIZE - n,
		      "Accept: text/html,application/xhtml+xml,application/"
		      "xml;q=0.9,*/*;q=0.8\r\n");
	n += snprintf(conn->buf + n, BUFSIZE - n,
		      "Accept-Language: en-us,en;q=0.5\r\n");
	n += snprintf(conn->buf + n, BUFSIZE - n,
		      "Accept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\n");
#endif

#ifdef WANT_GZIP
	n += snprintf(conn->buf + n, BUFSIZE - n,
		      "Accept-Encoding: gzip, deflate\r\n");
#endif

#ifdef FULL_HEADER
	if (proxy)
		n += snprintf(conn->buf + n, BUFSIZE - n,
					  "Proxy-Connection: keep-alive\r\n");
	else
		n += snprintf(conn->buf + n, BUFSIZE - n,
					  "Connection: keep-alive\r\n");
#endif
}

static int open_socket(struct connection *conn, char *host)
{
	char *port, *p;

	if (proxy)
		return connect_socket(conn, proxy, proxy_port);

	p = strchr(host, ':');
	if (p) {
		/* port specified */
		*p++ = '\0';
		port = p;
	} else
		port = is_https(conn->url) ? "443" : "80";

	return connect_socket(conn, host, port);
}

static int hostcmp(char *host1, char *host2)
{
	char *h[2] = { host1, host2 };
	int i;

	for (i = 0; i < 2; ++i)
		if (strncmp(h[i], "http://", 7) == 0)
			h[i] += 7;
		else if (strncmp(h[i], "https://", 8) == 0)
			h[i] += 8;

	return strcmp(h[0], h[1]);
}

int build_request(struct connection *conn)
{
	char *url, *host, *p;

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

#ifdef REUSE_SOCKET
	if (CONN_OPEN) {
		if (hostcmp(conn->host, host)) {
			if (verbose)
				printf("New connection for %s\n", host);
			release_connection(conn);
		} else if (verbose)
			printf("Reuse connection for %s\n", conn->host);
	}
#endif

	if (!CONN_OPEN)
		if (open_socket(conn, host)) {
			printf("Connection failed to %s\n", host);
			free(host);
			return 1;
		}

	if (proxy)
		snprintf(conn->buf, BUFSIZE, "%s http://%s/%s %s\r\n",
				method, host, url, http);
	else if (strchr(url, ' ')) {
		/* Some sites cannot handle spaces in the url. */
		int n = sprintf(conn->buf, "%s ", method);
		char *in = url, *out = conn->buf + n;
		while (*in)
			if (*in == ' ') {
				*out++ = '%';
				*out++ = '2';
				*out++ = '0';
				++in;
			} else
				*out++ = *in++;
		sprintf(out, " %s\r\n", http);
	} else
		snprintf(conn->buf, BUFSIZE, "%s %s %s\r\n", method, url, http);

	add_full_header(conn, host);

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


void write_request(struct connection *conn)
{
	int n; /* must be signed. windows does not support ssize_t */

#ifdef WANT_SSL
	if (conn->ssl) {
		n = openssl_write(conn);
		/* openssl_write can return -EAGAIN if the SSL
		 * connection needs a read or write. */
		if (n == -EAGAIN)
			return;
	} else
#endif
		n = send(conn->poll->fd, conn->curp, conn->length,
			 MSG_NOSIGNAL);

	if (n == conn->length) {
		if (verbose > 2)
			printf("+ Sent request\n");
		conn->length = 0;

		/* reset for read */
		set_readable(conn);
		reset_buf(conn);
		NEXT_STATE(conn, read_reply);
	} else if (n > 0) {
		conn->length -= n;
		conn->curp += n;
	} else {
		printf("Write request error\n");
		fail_connection(conn);
	}
}

static int redirect(struct connection *conn, int status)
{
	char *p = strstr(conn->buf, "Location:");
	if (p) {
		char *e;

		for (p += 9; isspace(*p); ++p)
			;
		e = strchr(p, '\n');
		if (e) {
			while (isspace(*(e - 1)))
				--e;
			*e = '\0';
			if (!conn->redirect_ok || verbose > 1)
				printf("WARNING: %s redirected to %s\n",
				       conn->url, p);
			release_connection(conn);
			free(conn->url);

			if (is_http(p))
				conn->url = strdup(p);
			else { /* Relative URL */
				int len = strlen(conn->host) + strlen(p) + 2;
				conn->url = malloc(len);
				if (conn->url)
					sprintf(conn->url, "%s/%s", conn->host, p);
			}

			if (!conn->url) {
				printf("Out of memory\n");
				return 1;
			}

			/* This will cause a bogus Multiple
			 * Closes error if it fails. */
			if (build_request(conn))
				return fail_redirect(conn);

			return 0;
		}
	}

	printf("%s: %d with no new location\n", conn->host, status);
	return status;
}

/* State function */
int read_reply(struct connection *conn)
{
	char *p, *fname;
	int status = 1;
	int chunked = 0;
	int needopen = 1;

	p = strstr(conn->buf, "\n\r\n");
	if (p) {
		*(p + 1) = '\0';
		conn->curp = p + 3;
		if (verbose > 1)
			printf("- Reply %d bytes\n",
			       (int)(conn->curp - conn->buf));
	} else if (conn->curp == conn->endp) {
		printf("Unexpected reply EOF %s\n", conn->url);
		return 1;
	} else if (conn->rlen > 0) {
		/* I have never seen this happen */
		conn->curp = conn->endp;
		return 0;
	} else {
		/* I have seen this once: the reply did not contain
		 * CRs, just LFs */
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
		p = strstr(conn->buf, "Content-Encoding:");
		if (p) {
			p += 17;
			while (isspace(*p))
				++p;
			if (strncmp(p, "gzip", 4) == 0) {
				if (verbose > 1)
					printf("GZIP\n");
				if (gzip_init(conn))
					return 1;
			} else
				printf("OH oh. %s: %s", conn->host, p);
		}
		if (verbose > 1 && conn->length == 0 && !chunked)
			printf("Warning: No content length for %s\n",
				   conn->url);
		break;

	case 301: /* Moved Permanently */
	case 302: /* Moved Temporarily */
		return redirect(conn, status);

	case 0:
		printf("HUH? NO STATUS\n");
		status = 2;
		/* fall thru */
	default:
		printf("%d: %s\n", status, conn->url);
		return status;
	}

	if (*method == 'H') {
		/* for head request we are done */
		close_connection(conn);
		return 0;
	}

	if (conn->regexp && !conn->matched)
		fname = conn->regfname;
	else
		needopen = 0; /* defer open */

	if (needopen) {
		conn->out = open(fname, WRITE_FLAGS, 0664);
		if (conn->out < 0) {
			my_perror(fname);
			return 1;
		}

		if (verbose > 1)
			printf("Output %s -> %s\n", conn->url, fname);
	} else if (verbose > 1)
		printf("Output %s deferred\n", conn->url);

	if (chunked) {
		conn->cstate = CS_DIGITS;
		NEXT_STATE(conn, read_file_chunked);
		conn->length = 0; /* paranoia */
	} else if (conn->zs) {
		NEXT_STATE(conn, read_file_gzip);
	} else if (conn->length == 0)
		NEXT_STATE(conn, read_file_unsized);
	else
		NEXT_STATE(conn, read_file);

	if (conn->curp < conn->endp)
		return conn->func(conn);

	return 0;
}


/* This is the only place we write to the output file */
static int write_output(struct connection *conn, int bytes)
{
	int n;

	if (conn->out == -1) { /* deferred open */
		/* We alloced space for the extension in add_outname */
		char *buf = conn->zs ? (char *)conn->zs_buf : conn->curp;
		if (want_extensions)
			strcat(conn->outname, lazy_imgtype(buf));

		conn->out = open(conn->outname, WRITE_FLAGS, 0664);
		if (conn->out < 0) {
			my_perror(conn->outname);
			return 0;
		}

		if (verbose > 1)
			printf("Output %s -> %s\n", conn->url, conn->outname);
	}

	if (conn->zs)
		n = write(conn->out, conn->zs_buf, bytes);
	else
		n = write(conn->out, conn->curp, bytes);

	if (n != bytes) {
		if (n < 0)
			printf("%s: Write error: %s\n",
				   conn->outname, strerror(errno));
		else
			printf("%s: Write error: %d/%d\n",
				   conn->outname, n, bytes);
		return 0;
	}

	return bytes;
}


/* State function */
static int read_chunkblock(struct connection *conn)
{
	size_t bytes;

	bytes = conn->endp - conn->curp;
	if (bytes > (size_t)conn->length)
		bytes = conn->length;

	if (bytes > 0) {
		if (conn->zs) {
			if (write_output_gzipped(conn, bytes) < 0) {
				printf("Gzipped write error\n");
				return 1;
			}
		} else if (!write_output(conn, bytes))
			return 1;
	}

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
		else if (conn->rlen == 0)
			reset_buf(conn);
		return 0;
	}

	reset_buf(conn);
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
		printf("Hmmm, %s already empty (%d)\n", conn->url, conn->rlen);
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

	if (conn->curp == conn->endp)
		return 0; /* Need to read next chunk */

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

#ifdef WANT_GZIP
static int gzip_init(struct connection *conn)
{
	conn->zs = calloc(1, sizeof(z_stream));
	if (!conn->zs) {
		printf("Out of memory\n");
		return 1;
	}

	conn->zs_buf = malloc(BUFSIZE);
	if (!conn->zs_buf) {
		printf("Out of memory\n");
		return 1;
	}

	/* Window size 15 is default for zlib. Adding 32 allows us to
	 * handle gzip format. */
	if (inflateInit2(conn->zs, 15 + 32))
		return 1;

	return 0;
}

static int write_output_gzipped(struct connection *conn, size_t bytes)
{
	int rc, sz;
	z_stream *zs = conn->zs;
	zs->next_in = (unsigned char *)conn->curp;
	zs->avail_in = bytes;

	/* Inflate and write all output until we are done with the
	 * input buffer. */
	do {
		zs->next_out = conn->zs_buf;
		zs->avail_out = BUFSIZE;

		rc = inflate(zs, Z_SYNC_FLUSH);

		switch (rc) {
		case Z_BUF_ERROR:
			rc = 0;
			break; /* not a problem */

		case Z_OK:
		case Z_STREAM_END:
			sz = BUFSIZE - zs->avail_out;
			if (sz <= 0)
				return rc;
			if (!write_output(conn, sz))
				return -1;
			break;

		default:
			printf("Inflate failed: %d\n", rc);
			return -1;
		}
	} while (zs->avail_out == 0);

	return rc;
}

/* State function */
static int read_file_gzip(struct connection *conn)
{
	size_t bytes;
	int rc;

	bytes = conn->endp - conn->curp;
	if (bytes <= 0) {
		printf("Read file problems %zu for %s!\n", bytes, conn->url);
		return 1;
	}

	rc = write_output_gzipped(conn, bytes);
	if (rc < 0)
		return 1;

	conn->length -= bytes;
	if (conn->length <= 0 || rc == Z_STREAM_END) {
		if (verbose)
			printf("OK %s\n", conn->url);
		if (conn->regexp && !conn->matched)
			return process_html(conn);
		close_connection(conn);
		return 0;
	}

	reset_buf(conn);
	return 0;
}
#else
static int gzip_init(struct connection *conn)
{
	puts("Sorry, no GZIP support.");
	return 1;
}

static int read_file_gzip(struct connection *conn)
{
	return 1;
}

static int write_output_gzipped(struct connection *conn, size_t bytes)
{
	return -1;
}
#endif

/* State function */
static int read_file_unsized(struct connection *conn)
{
	size_t bytes;

	bytes = conn->endp - conn->curp;
	if (bytes > 0) {
		if (!write_output(conn, bytes))
			return 1;
	} else {
		if (verbose)
			printf("OK %s\n", conn->url);
		if (conn->regexp && !conn->matched)
			return process_html(conn);
		close_connection(conn);
		return 0;
	}

	reset_buf(conn);
	return 0;
}


/* State function */
static int read_file(struct connection *conn)
{
	size_t bytes;

	bytes = conn->endp - conn->curp;
	if (bytes > 0) {
		if (!write_output(conn, bytes))
			return 1;
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
		printf("Read file problems %zu for %s!\n", bytes, conn->url);
		return 1;
	}

	reset_buf(conn);
	return 0;
}

static struct pollfd *ufds;

static int timeout_connections(void)
{
	struct connection *comic;
	time_t timeout = time(NULL) - read_timeout;

	for (comic = comics; comic; comic = comic->next)
		if (comic->poll && comic->access < timeout) {
			printf("TIMEOUT %s\n", comic->url);
			fail_connection(comic);
		}

	return 0;
}

static void read_conn(struct connection *conn)
{
	int n;

	time(&conn->access);
#ifdef WANT_SSL
	if (conn->ssl) {
		n = openssl_read(conn);
		/* openssl_read can return -EAGAIN if the SSL
		 * connection needs a read or write. */
		if (n == -EAGAIN)
			return;
	} else
#endif
		do
			n = recv(conn->poll->fd, conn->curp, conn->rlen, 0);
		while (n < 0 && errno == EINTR);
	if (n >= 0) {
		if (verbose > 1)
			printf("+ Read %d/%d\n", n, conn->rlen);
		if (n > 0) {
			conn->endp = conn->curp + n;
			conn->rlen -= n;
			*conn->endp = '\0';
		}

		if (conn->func && conn->func(conn))
			fail_connection(conn);
	} else if (n < 0)
		reset_connection(conn); /* Try again */
}

void main_loop(void)
{
	int i, n, timeout = 250;
	struct connection *conn;

	ufds = must_calloc(thread_limit, sizeof(struct pollfd));
	for (i = 0; i < thread_limit; ++i)
		ufds[i].fd = -1;

	while (head || outstanding) {
		start_next_comic();

		n = poll(ufds, thread_limit, timeout);
		if (n < 0) {
			my_perror("poll");
			continue;
		}

		if (n == 0) {
			timeout_connections();
			if (!start_next_comic())
				/* Once we have all the comics
				 * started, increase the timeout
				 * period. */
				timeout = 1000;
			continue;
		}

		for (conn = comics; conn; conn = conn->next)
			if (!conn->poll)
				continue;
			else if (conn->poll->revents & POLLOUT) {
				if (!conn->connected)
					check_connect(conn);
				else {
					time(&conn->access);
					write_request(conn);
				}
			} else if (conn->poll->revents & POLLIN) {
				/* This check is needed for openssl */
				if (!conn->connected)
					check_connect(conn);
				else
					read_conn(conn);
			}
	}

	free(ufds);
}

int set_conn_socket(struct connection *conn, int sock)
{
	int i;

	for (i = 0; i < thread_limit; ++i)
		if (ufds[i].fd == -1) {
			conn->poll = &ufds[i];
			conn->poll->fd = sock;
			/* All sockets start out writable */
			conn->poll->events = POLLOUT;
			return 1;
		}

	return 0;
}
