#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
/* winsock2.h must be before windows.h to avoid winsock.h clashes */
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <direct.h> /* for chdir */
#else
#include <unistd.h>
#include <sys/time.h>
#endif


#define HTTP_PORT		80
#define XML_FILE		"/usr/share/get-comics/comics.xml"


/* Use the internal http code, else use wget. See comment below. */
#define USE_INTERNAL	1


/*
 * Limit the number of concurrent threads.
 *
 * The internal routine should be able to do three times the threads
 * as the wget version. Why? The reason I limited the threads was that
 * my poor little 'Winder could not handle 31 consecutive wgets. Each
 * wget needs one thread and 2 processes (a shell and the wget). With
 * the internal model, we only have the thread.
 *
 * Which is all moot since my provider seems to limit simultaneous
 * connections :-(
 *
 */
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

	int sock;
	int connected;
	FILE *out;
	time_t access;

	char buf[BUFSIZE + 1];
	int  bufn;
	int  rlen;
	char *curp; /* for chunking */
	char *endp; /* SAM BYTES? for chunking */
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

	struct connection *next;
};

extern char *comics_dir;
extern struct connection *comics;
extern int n_comics;
extern int skipped;
extern int verbose;
extern int thread_limit;
extern int threads_set;
extern int read_timeout;
extern int randomize;
extern int run_m4;

void my_perror(char *str);
#define perror(s)	Do not use

int fail_connection(struct connection *conn);
int close_connection(struct connection *conn);
int release_connection(struct connection *conn);
int process_html(struct connection *conn);

int set_readable(int sock);
int set_writable(int sock);

/* export from xml.c */
int read_config(char *fname);
void set_failed(char *fname);
int write_comic(struct connection *conn);
char *must_strdup(char *str);

/* export from http2.c */
void set_proxy(char *proxystr);
char *get_proxy(void);
int write_request(struct connection *conn);
int read_reply(struct connection *conn);
int build_request(struct connection *conn);

#ifdef _WIN32
/* We only use read/write/close on sockets */
/* We use stream operations on files */
#define close closesocket
#define read(s, b, n)  recv(s, b, n, 0)
#define write(s, b, n) send(s, b, n, 0)

#define socklen_t int

#define unlink _unlink
#define strdup _strdup
#define chdir _chdir
#define stricmp _stricmp
#define inline _inline

/* from win32.c */
void win32_init(void);
#endif
