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
