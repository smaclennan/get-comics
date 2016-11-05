#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include "win32/win32.h"

#define JSON_FILE		"../comics.json"
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/socket.h>

#define JSON_FILE		"/usr/share/get-comics/comics.json"
#endif

#ifdef WANT_GZIP
#include <zlib.h>
#else
#define z_stream void
static inline int inflateEnd(void *strm) { return -1; }
#endif

#ifdef WANT_CURL
#include <curl/curl.h>
#endif

#define HTTP_PORT		80

/* Limit the number of concurrent sockets. */
#define THREAD_LIMIT	100

/*
 * Maximum length of time to wait for a read/write
 * In seconds
 */
#define SOCKET_TIMEOUT	(2 * 60)

/* The depth of the regexp matchs. */
/* Affects the maximum value of the <regmatch> tag */
#define MATCH_DEPTH		4

/* I seem to get 1440 byte "chunks". However, if the connection is
 * slow, you will get more bytes. Basically, the bigger the buffer the
 * better if congested.
 *
 * On my slow DSL (2016), it seems to peak out about 64k.
 */
#define BUFSIZE		(64 * 1024)

struct log {
	char **events;
	int n_events;
	int max_events;
	int failed;
};


struct connection {
	int id; /* for debugging */
	char *url;
	char *host; /* filled by read_config */
	char *regexp;
	char *regfname;
	int   regmatch;
	int   matched;
	char *outname;
	char *base_href;
	char *referer; /* king features needs this */
	unsigned days; /* bitmask */
	int   gotit;
	int reset;
	int redirect_ok;

	int connected;
	int out;
	time_t access;

#ifdef WANT_CURL
	CURL *curl;
#ifdef MULTI_THREADED
	pthread_t thread;
#endif
#else
	struct pollfd *poll;

	char *buf;
	int  rlen;
	char *curp; /* for chunking */
	char *endp; /* for chunking */
	z_stream *zs; /* for gzip */
	unsigned char *zs_buf; /* for gzip */
	int  length; /* content length if available */
	enum {
		CS_NONE,
		CS_START_CR,
		CS_START_LF,
		CS_DIGITS,
		CS_END_CR,
		CS_END_LF
	} cstate; /* chunk state */
	int (*func)(struct connection *conn);
#define NEXT_STATE(c, f)  ((c)->func = (f))

#ifdef WANT_SSL
	void *ssl;
#endif
#endif /* WANT_CURL */

	struct log *log;

	struct connection *next;
};

#ifdef WANT_CURL
#define CONN_OPEN (conn->curl)
#else
#define CONN_OPEN (conn->poll)
#endif

extern struct connection *comics;
extern struct connection *head;

extern char *comics_dir;
extern int n_comics;
extern int skipped;
extern int verbose;
extern int thread_limit;
extern int threads_set;
extern int read_timeout;
extern int want_extensions;
extern int unlink_index;
extern FILE *debug_fp;
extern FILE *links_only;

extern char *proxy;
extern char *proxy_port;

extern const char *user_agent;

extern int outstanding;
extern int gotit;
extern int resets;

extern const char *method;

#ifndef O_BINARY
#define O_BINARY 0
#endif
#define WRITE_FLAGS (O_CREAT | O_TRUNC | O_WRONLY | O_BINARY)

#ifndef _WIN32
#define closesocket close
#endif

static inline char *is_http(char *p)
{
	if (strncmp(p, "http://", 7) == 0)
		return p + 7;
#ifdef WANT_SSL
	if (strncmp(p, "https://", 8) == 0)
		return p + 8;
#endif
	return NULL;
}

static inline int is_https(char *p)
{
	return strncmp(p, "https://", 8) == 0;
}

/* Send to stdout, not stderr */
static inline void my_perror(const char *str)
{
#ifdef _WIN32
	printf("%s: error %d\n", str, WSAGetLastError());
#else
	printf("%s: %s\n", str, strerror(errno));
#endif
}
#define perror(s)	Do_not_use_perror

int reset_connection(struct connection *conn);
int fail_connection(struct connection *conn);
int release_connection(struct connection *conn);
int close_connection(struct connection *conn);
int process_html(struct connection *conn);
void do_add_regexp(struct connection *conn, const char *regexp, const char *index_dir);

#ifdef WANT_CURL
static inline void set_writable(struct connection *conn) {}
#else
static inline void set_readable(struct connection *conn)
{
	conn->poll->events = POLLIN;
}

static inline void set_writable(struct connection *conn)
{
	conn->poll->events = POLLOUT;
}

int set_conn_socket(struct connection *conn, int sock);
#endif

char *must_strdup(const char *str);
void *must_calloc(int nmemb, int size);
static inline void *must_alloc(int size) { return must_calloc(1, size); }
char *lazy_imgtype(char *buf);
int is_imgtype(const char *ext);
void dump_outstanding(int sig);
char *create_outname(char *url);
char *fixup_url(char *url, char *tmp, int len);

/* export from config.c */
int read_config(const char *fname);
void add_index_dir(const char *dir);
void clean_index_dir(void);

/* export from http.c */
void set_proxy(char *proxystr);
void write_request(struct connection *conn);
int read_reply(struct connection *conn);
int build_request(struct connection *conn);
void out_results(struct connection *comics, int skipped);

/* export from socket.c */
int connect_socket(struct connection *conn, char *hostname, char *port);
void check_connect(struct connection *conn);
void free_cache(void);

/* export from log.c */
void log_add(struct connection *conn, char *fmt, ...);
void log_want_dump(struct connection *conn);
void log_dump(struct connection *conn);
void log_clear(struct connection *conn);

/* export from openssl.c */
int openssl_connect(struct connection *conn);
int openssl_check_connect(struct connection *conn);
int openssl_read(struct connection *conn);
int openssl_write(struct connection *conn);
void openssl_close(struct connection *conn);
void openssl_list_ciphers(void);

/* export from get-comics.c */
int start_next_comic(void);
int start_one_comic(struct connection *conn);

void main_loop(void);
