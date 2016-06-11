#include "get-comics.h"
#include <regex.h>

/* Globals and some helper functions common to all the executables. */

int verbose;
int outstanding;
int gotit;
int resets;
int n_comics;
int read_timeout = SOCKET_TIMEOUT;
int want_extensions;
const char *method = "GET";
int thread_limit = THREAD_LIMIT;
int unlink_index = 1;


struct connection *comics;
struct connection *head;

FILE *debug_fp;
FILE *links_only;

char *proxy;
char *proxy_port = "3128";

const char *user_agent =
	"Mozilla/5.0 (X11; Linux i686; rv:6.0.2) Gecko/20100101 Firefox/6.0.2";

char *must_strdup(const char *old)
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

/* This is a very lazy checking heuristic since we expect the files to
 * be one of the four formats and well formed. Yes, Close To Home
 * actually used TIFF. TIFF is only tested on little endian machines. */
char *lazy_imgtype(char *buf)
{
	int i;

	for (i = 0; i < sizeof(hdrs) / sizeof(struct header); ++i)
		if (memcmp(buf, hdrs[i].hdr, 4) == 0)
			return hdrs[i].ext;

	return ".xxx";
}

int is_imgtype(const char *ext)
{
	int i;

	for (i = 0; i < sizeof(hdrs) / sizeof(struct header); ++i)
		if (strcmp(ext, hdrs[i].ext) == 0)
			return 1;

	return strcmp(ext, ".xxx") == 0;
}

/* Normal way to close connection */
int close_connection(struct connection *conn)
{
	if (CONN_OPEN) {
		++gotit;
		conn->gotit = 1;
		--outstanding;
		if (verbose > 1)
			printf("Closed %s (%d)\n", conn->url, outstanding);
		if (debug_fp)
			fprintf(debug_fp, "%ld:   Closed %3d (%d)\n",
					time(NULL), conn->id, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
}


/* Abnormal way to close connection */
int fail_connection(struct connection *conn)
{
	if (CONN_OPEN) {
		--outstanding;
		if (verbose > 1)
			printf("Failed %s (%d)\n", conn->url, outstanding);
		if (debug_fp)
			fprintf(debug_fp, "%ld:   Failed %3d (%d)\n",
					time(NULL), conn->id, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
}

#ifndef WANT_CURL
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
#endif

static void add_link(struct connection *conn)
{
	if (verbose)
		printf("Add link %s\n", conn->url);

	fprintf(links_only, "%s\n", conn->url);

	conn->gotit = 1;
	++gotit;
}

int start_one_comic(struct connection *conn)
{
	if (links_only && !conn->regexp) {
		add_link(conn);
		return 0;
	} else if (build_request(conn) == 0) {
		time(&conn->access);
		if (verbose)
			printf("Started %s (%d)\n", conn->url, outstanding);
		if (debug_fp)
			fprintf(debug_fp, "%ld: Started  %3d '%s' (%d)\n",
					conn->access, conn->id,
					conn->outname ? conn->outname : conn->url, outstanding);
		++outstanding;
		return 1;
	}

	printf("build_request %s failed\n", conn->url);
	if (debug_fp) {
		time(&conn->access);
		fprintf(debug_fp, "%ld: Start failed  %3d '%s' (%d)\n",
				conn->access, conn->id,
				conn->outname ? conn->outname : conn->url, outstanding);
	}
	return 0;
}

int start_next_comic(void)
{
	while (head && outstanding < thread_limit) {
		int rc = start_one_comic(head);
		head = head->next;
		if (rc)
			return rc;
	}

	return head != NULL;
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

void dump_outstanding(int sig)
{
	struct connection *conn;
	struct tm *atime;
	time_t now = time(NULL);

	atime = localtime(&now);
	printf("\nTotal %d Outstanding: %d @ %2d:%02d:%02d\n",
		   n_comics, outstanding,
		   atime->tm_hour, atime->tm_min, atime->tm_sec);
	for (conn = comics; conn; conn = conn->next) {
		if (!CONN_OPEN)
			continue;
		printf("> %s = %s\n", conn->url,
			   conn->connected ? "connected" : "not connected");
		if (conn->regexp)
			printf("  %s %s\n",
				   conn->matched ? "matched" : "regexp",
				   conn->regexp);
		atime = localtime(&conn->access);
		printf("  %2d:%02d:%02d\n",
			   atime->tm_hour, atime->tm_min, atime->tm_sec);
	}
	for (conn = head; conn; conn = conn->next)
		printf("Q %s\n", conn->url);
	fflush(stdout);
}

char *create_outname(char *url)
{
	char *fname;
	char *p = strrchr(url, '/');
	if (p) {
		++p;
		if (*p)
			fname = p;
		else
			fname = "index.html";
	} else
		fname = url;

	char *outname = must_alloc(strlen(fname) + 4 + 1);
	return strcpy(outname, fname);
}

static char *find_regexp(struct connection *conn, char *reg, int regsize)
{
	FILE *fp;
	regex_t regex;
	regmatch_t match[MATCH_DEPTH];
	int err, mn = conn->regmatch;
	/* Max line I have seen is 114k from comics.com! */
	char buf[128 * 1024];

	err = regcomp(&regex, conn->regexp, REG_EXTENDED);
	if (err) {
		char errstr[200];

		regerror(err, &regex, errstr, sizeof(errstr));
		printf("%s\n", errstr);
		regfree(&regex);
		return NULL;
	}

	fp = fopen(conn->regfname, "r");
	if (!fp) {
		my_perror(conn->regfname);
		return NULL;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		if (regexec(&regex, buf, MATCH_DEPTH, match, 0) == 0) {
			/* got a match */
			fclose(fp);
			regfree(&regex);
			if (unlink_index)
				unlink(conn->regfname);

			if (match[mn].rm_so == -1) {
				printf("%s did not have match %d\n",
					   conn->url, mn);
				return NULL;
			}

			*(buf + match[mn].rm_eo) = '\0';
			snprintf(reg, regsize, "%s", buf + match[mn].rm_so);
			return reg;
		}
	}

	if (ferror(fp))
		printf("PROBLEMS\n");

	fclose(fp);

	printf("%s DID NOT MATCH REGEXP\n", conn->url);
	regfree(&regex);

	return NULL;
}

int process_html(struct connection *conn)
{
	char imgurl[1024], regmatch[1024], *p;

	if (conn->out >= 0) {
		close(conn->out);
		conn->out = -1;
	}

	p = find_regexp(conn, regmatch, sizeof(regmatch));
	if (p == NULL)
		return 1;

	/* We are done with this socket, but not this connection */
	release_connection(conn);
	free(conn->url);

	if (verbose > 1)
		printf("Matched %s\n", p);

	/* For the writer we need to know if url was modified */
	conn->matched = 1;

	if (is_http(p))
		/* fully rooted */
		conn->url = strdup(p);
	else if (strncmp(p, "//", 2) == 0) {
		/* partially rooted - let's assume http */
		snprintf(imgurl, sizeof(imgurl), "http:%s", p);
		conn->url = strdup(imgurl);
	} else {
		if (conn->base_href)
			snprintf(imgurl, sizeof(imgurl), "%s%s", conn->base_href, p);
		else if (*p == '/')
			snprintf(imgurl, sizeof(imgurl), "%s%s", conn->host, p);
		else
			snprintf(imgurl, sizeof(imgurl), "%s/%s", conn->host, p);
		conn->url = strdup(imgurl);
	}

	if (links_only) {
		add_link(conn);
		--outstanding;
		return 0;
	}

	if (build_request(conn) == 0)
		set_writable(conn);
	else
		printf("build_request %s failed\n", conn->url);

	if (verbose)
		printf("Started %s\n", conn->url);

	return 0;
}

void do_add_regexp(struct connection *conn, const char *regexp, const char *index_dir)
{
	static int unique;

	conn->regexp = must_strdup(regexp);
	if (conn->regfname == NULL) {
		char out[256];

		if (index_dir)
			snprintf(out, sizeof(out), "%s/index-%08x.html", index_dir, ++unique);
		else
			snprintf(out, sizeof(out), "index-%08x.html", ++unique);
		conn->regfname = must_strdup(out);
	}
}
