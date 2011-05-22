#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#ifdef _WIN32
#include "win32/win32.h"
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/poll.h>
#endif
#include <sys/stat.h>


#define HTTP_PORT		80
#define JSON_FILE		"/usr/share/get-comics/comics.json"


/* Limit the number of concurrent sockets. */
#define THREAD_LIMIT	10

/*
 * Maximum length of time to wait for a read
 * In milliseconds
 */
#define READ_TIMEOUT	(5 * 60 * 1000)

/*
 * Maximum length of time to wait for a read/write
 * In seconds
 */
#define SOCKET_TIMEOUT	(5 * 60)

/* The depth of the regexp matchs. */
/* Affects the maximum value of the <regmatch> tag */
#define MATCH_DEPTH		4

/* I seem to get 1360 byte "chunks"
 * This seems a good compromise
 */
#define BUFSIZE		2048

struct log {
	char **events;
	int n_events;
	int max_events;
	int failed;
};

struct connection {
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

	struct pollfd *poll;
	int connected;
	FILE *out;
	time_t access;

	char buf[BUFSIZE + 1];
	int  bufn;
	int  rlen;
	char *curp; /* for chunking */
	char *endp; /* for chunking */
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

	struct log *log;

#ifdef WANT_SSL
	void *ssl;
#endif

	struct connection *next;
};

extern char *comics_dir;
extern int n_comics;
extern int skipped;
extern int verbose;
extern int thread_limit;
extern int threads_set;
extern int read_timeout;
extern int randomize;

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
static inline void my_perror(char *str)
{
#ifdef _WIN32
	printf("%s: error %d\n", str, WSAGetLastError());
#else
	printf("%s: %s\n", str, strerror(errno));
#endif
}
#define perror(s)	Do_not_use_perror

int close_connection(struct connection *conn);
int fail_connection(struct connection *conn);
int fail_redirect(struct connection *conn);
int release_connection(struct connection *conn);
int process_html(struct connection *conn);

static inline void set_readable(struct connection *conn)
{
	conn->poll->events = POLLIN;
}

static inline void set_writable(struct connection *conn)
{
	conn->poll->events = POLLOUT;
}

static inline int inprogress(void)
{
#ifdef _WIN32
	return WSAGetLastError() == WSAEWOULDBLOCK;
#else
	return errno == EINPROGRESS;
#endif
}


int set_conn_socket(struct connection *conn, int sock);
void add_comic(struct connection *comic);
char *must_strdup(char *str);

/* export from config.c */
int read_config(char *fname);
void write_comic(struct connection *conn);

/* export from http.c */
void set_proxy(char *proxystr);
char *get_proxy(void);
void check_connect(struct connection *conn);
void write_request(struct connection *conn);
int read_reply(struct connection *conn);
int build_request(struct connection *conn);

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
