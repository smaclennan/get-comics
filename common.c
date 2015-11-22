#include "get-comics.h"

/* Globals and some helper functions common to all the executables. */

int verbose;
int outstanding;
int gotit;
int resets;
int n_comics;
int read_timeout = SOCKET_TIMEOUT;
const char *method = "GET";

FILE *debug_fp;

char *proxy;
char *proxy_port = "3128";

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

/* This is a very lazy checking heuristic since we expect the files to
 * be one of the four formats and well formed. Yes, Close To Home
 * actually used TIFF. TIFF is only tested on little endian machines. */
char *lazy_imgtype(char *buf)
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

	for (i = 0; i < sizeof(hdrs) / sizeof(struct header); ++i)
		if (memcmp(buf, hdrs[i].hdr, 4) == 0)
			return hdrs[i].ext;

	return ".xxx";
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
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
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

