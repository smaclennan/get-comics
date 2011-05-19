#include "get-comics.h"

struct tm *today;
unsigned wday;


static int file_access(char *fname)
{
	struct stat sbuf;

	if (stat(fname, &sbuf))
		return errno;

	/* This allows files and links */
	errno = EISDIR;
	return S_ISREG(sbuf.st_mode) == 0;
}

static char *pick_filename(void)
{
#ifdef WANT_JSON
	if (file_access(JSON_FILE) == 0)
		return JSON_FILE;
#endif

#ifdef WANT_XML
	if (file_access(XML_FILE) == 0)
		return XML_FILE;
#endif

	printf("No config files found\n");
	exit(1);
}

int read_config(char *fname)
{
	time_t now;

	if (!fname)
		fname = pick_filename();

	if (file_access(fname)) {
		my_perror(fname);
		exit(1);
	}

	/* Get the time for the urls */
	time(&now);
	today = localtime(&now);
	wday = 1 << today->tm_wday;

#ifdef WANT_JSON
#  ifdef WANT_XML
	{
		char *p = strrchr(fname, '.');
		if (p && strcasecmp(p, ".xml") == 0)
			return read_xml_config(fname);
	}
#  endif
	return read_json_config(fname);
#elif defined(WANT_XML)
	return read_xml_config(fname);
#else
#error Need config defined. See Makefile
#endif
}

/* Helpers for the read_X_config functions */
struct connection *new_comic(void)
{
	struct connection *new = calloc(1, sizeof(struct connection));
	if (!new) {
		printf("OUT OF MEMORY\n");
		exit(1);
	}
	return new;
}

void sanity_check_comic(struct connection *new)
{
	if (!new)
		printf("WARNING: Empty comic entry\n");
	else if (!new->url) {
		printf("ERROR: comic with no url!\n");
		exit(1);
	} else if (!new->host) {
		printf("ERROR: comic with no host!\n");
		exit(1);
	} else if (new->days && (new->days & wday) == 0) {
		if (verbose)
			printf("Skipping %s\n", new->url);
		++skipped;
		free(new);
	} else
		add_comic(new);
}

void add_url(struct connection **conn, char *url)
{
	char outurl[1024], *e;

	if (*conn == NULL)
		*conn = new_comic();

	if (strftime(outurl, sizeof(outurl), url, today) == 0) {
		printf("strftime failed for '%s'\n", url);
		exit(1);
	}

	if ((*conn)->url)
		printf("Url already set. Ignoring %s\n", url);
	else {
		(*conn)->url = must_strdup(outurl);

		/* isolate the host from original url */
		e = is_http(url);
		if (e)
			e = strchr(e, '/');
		else
			e = strchr(url + 1, '/');
		if (e)
			*e = '\0';

		(*conn)->host = must_strdup(url);
	}
}

void add_days(struct connection **conn, char *days)
{
	unsigned i;

	if (*conn == NULL)
		*conn = new_comic();

	for (i = 0; *days && i < 7; ++days, ++i)
		if (*days != 'X' && *days != 'x')
			(*conn)->days |= 1 << i;
}

void add_regexp(struct connection **conn, char *regexp)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->regexp = must_strdup(regexp);
	if ((*conn)->regfname == NULL) {
		char fname[20];

		sprintf(fname, "index-%08x.html", n_comics);
		(*conn)->regfname = must_strdup(fname);
	}
}

void add_regmatch(struct connection **conn, int match)
{
	if (*conn == NULL)
		*conn = new_comic();
	if (match >= MATCH_DEPTH)
		printf("<regmatch> %d too big.\n", match);
	else
		(*conn)->regmatch = match;
}

void add_outname(struct connection **conn, char *outname)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->outname = must_strdup(outname);
}

void add_base_href(struct connection **conn, char *base_href)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->base_href = must_strdup(base_href);
}

void add_referer(struct connection **conn, char *referer)
{
	if (*conn == NULL)
		*conn = new_comic();
	if (strcasecmp((char *)referer, "url") == 0 && (*conn)->url)
		(*conn)->referer = must_strdup((*conn)->url);
	else
		(*conn)->referer = must_strdup(referer);
}

void write_comic(struct connection *conn) {}

/*
 * Local Variables:
 * my-sparse-args: "-DWANT_XML -DWANT_JSON"
 * End:
 */
