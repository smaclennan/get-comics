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

const char *method = "GET";

static char *proxy;
static char *proxy_port = "3128";

static char *http = "HTTP/1.1";

int verbose;

int outstanding;
int gotit;
int n_comics;
static int resets;

#define MIN(a, b)	((a) < (b) ? (a) : (b))

static int read_file(struct connection *conn);
static int read_file_unsized(struct connection *conn);
static int read_file_chunked(struct connection *conn);
static int gzip_init(struct connection *conn);
static int read_file_gzip(struct connection *conn);
static int write_output_gzipped(struct connection *conn, size_t bytes);


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

/* Normal way to close connection */
static int close_connection(struct connection *conn)
{
	if (conn->poll) {
		++gotit;
		conn->gotit = 1;
		--outstanding;
		if (verbose > 1)
			printf("Closed %s (%d)\n", conn->url, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
}


/* Abnormal way to close connection */
int fail_connection(struct connection *conn)
{
	if (conn->poll) {
		write_comic(conn);
		--outstanding;
		if (verbose > 1)
			printf("Failed %s (%d)\n", conn->url, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
}

/* Fail a redirect. We have already released the connection. */
static int fail_redirect(struct connection *conn)
{
	if (conn->poll)
		printf("Failed redirect not closed: %s\n", conn->url);
	else {
		write_comic(conn);
		--outstanding;
		if (verbose > 1)
			printf("Failed redirect %s (%d)\n",
			       conn->url, outstanding);
	}
	log_clear(conn);
	return 0;
}

/* Reset connection - try again */
int reset_connection(struct connection *conn)
{
	++conn->reset;
	if (conn->reset == 1)
		++resets; /* only count each connection once */
	if (conn->reset > 2)
		return fail_connection(conn);

	release_connection(conn);

	if (build_request(conn))
		return fail_connection(conn);

	return 0;
}


void set_proxy(char *proxystr)
{
	char *p;

	if (proxy) {
		if (verbose)
			printf("WARNING: proxy set to %s:%s. Ignoring %s\n",
				   proxy, proxy_port, proxystr);
		return;
	}

	p = strrchr(proxystr, ':');
	if (p) {
		*p++ = '\0';
		proxy_port = must_strdup(p);
	}

	proxy = must_strdup(proxystr);

	if (verbose)
		printf("Proxy %s:%s\n", proxy, proxy_port);
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
		sprintf(p, "%s:%s", proxy, proxy_port);

	return p;
}

static void add_full_header(struct connection *conn)
{
#define FULL_HEADER
#ifdef FULL_HEADER
	/* Header strings taken from Firefox. ~300 bytes. */
	int n = strlen(conn->buf);

	n += snprintf(conn->buf + n, BUFSIZE - n,
		      "User-Agent: Mozilla/5.0 (X11; Linux i686; rv:6.0.2) "
		      "Gecko/20100101 Firefox/6.0.2\r\n");
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
	n += snprintf(conn->buf + n, BUFSIZE - n,
		      "Connection: keep-alive\r\n");
#endif
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
		sprintf(conn->buf, "%s http://%s %s %s\r\n",
			method, host, url, http);
	} else {
		char *port = is_https(conn->url) ? "443" : "80";

		p = strchr(host, ':');
		if (p) {
			/* port specified */
			*p++ = '\0';
			port = p;
		}

		if (connect_socket(conn, host, port)) {
			printf("Connection failed to %s\n", host);
			free(host);
			return 1;
		}

		if (strchr(url, ' ')) {
			/* Some sites cannot handle spaces in the url. */
			char *in = url, *out = conn->buf + 4;
			strcpy(conn->buf, method);
			strcat(conn->buf, " ");
			while (*in)
				if (*in == ' ') {
					*out++ = '%';
					*out++ = '2';
					*out++ = '0';
					++in;
				} else
					*out++ = *in++;
			sprintf(out, " %s\r\nHost: %s\r\n", http, host);
		} else
			sprintf(conn->buf, "%s %s %s\r\nHost: %s\r\n",
				method, url, http, host);

		add_full_header(conn);
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
					sprintf(conn->url, "%s/%s",
						conn->host, p);
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
		printf("Unexpected EOF %s\n", conn->url);
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
	else if (conn->outname == NULL) {
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
	} else
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


/* This is a very lazy checking heuristic since we expect the files to
 * be one of the four formats and well formed. Yes, Close To Home
 * actually used TIFF. TIFF is only tested on little endian machines. */
static char *lazy_imgtype(struct connection *conn)
{
	static struct header {
		char *ext;
		unsigned char hdr[4];
	} hdrs[] = {
		{ ".gif", { 'G', 'I', 'F', '8' } }, /* gif89 and gif87a */
		{ ".png", { 0x89, 'P', 'N', 'G' } },
		{ ".jpg", { 0xff, 0xd8, 0xff, 0xe0 } }, /* jfif */
		{ ".jpg", { 0xff, 0xd8, 0xff, 0xe1 } }, /* exif */
		{ ".jpg", { 0xff, 0xd8, 0xff, 0xee } }, /* Adobe */
		{ ".tif", { 'I', 'I', 42, 0 } }, /* little endian */
		{ ".tif", { 'M', 'M', 0, 42 } }, /* big endian */
	};
	int i;
	char *buf = conn->zs ? (char *)conn->zs_buf : conn->curp;

	for (i = 0; i < sizeof(hdrs) / sizeof(struct header); ++i)
		if (memcmp(buf, hdrs[i].hdr, 4) == 0)
			return hdrs[i].ext;

	printf("WARNING: Unknown file type %s\n", conn->outname);
	return ".xxx";
}

/* This is the only place we write to the output file */
static int write_output(struct connection *conn, int bytes)
{
	int n;

	if (conn->out == -1) { /* deferred open */
		/* We alloced space for the extension in add_outname */
		strcat(conn->outname, lazy_imgtype(conn));

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
		return 0;
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

	conn->curp = conn->buf;
	conn->rlen = conn->bufn;
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
		printf("Read file problems %zu for %s!\n",
		       bytes, conn->url);
		return 1;
	}

	conn->curp = conn->buf;
	conn->rlen = conn->bufn;
	return 0;
}

char *must_strdup(char *old)
{
	char *new = strdup(old);
	if (!new) {
		printf("OUT OF MEMORY\n");
		exit(1);
	}
	return new;
}

void *must_calloc(int nmemb, int size)
{
	void *new = calloc(nmemb, size);
	if (!new) {
		printf("OUT OF MEMORY\n");
		exit(1);
	}
	return new;
}

void out_results(struct connection *conn, int skipped)
{
	printf("Got %d of %d", gotit, n_comics);
	if (skipped)
		printf(" (Skipped %d)", skipped);
	if (resets)
		printf(" (Reset %d)", resets);
	putchar('\n');

	/* Dump the missed comics */
	for (; conn; conn = conn->next)
		if (!conn->gotit) {
			if (conn->outname)
				printf("  %s (%s)\n", conn->url, conn->outname);
			else
				printf("  %s\n", conn->url);
		}

}
