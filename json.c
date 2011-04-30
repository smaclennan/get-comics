/* SAM Currently we only support one of XML or JSON. */
#if defined(WANT_JSON) && defined(WANT_XML)
#error You cannot have your cake and eat it too.
#endif

#ifdef WANT_JSON
#include "get-comics.h"

#define __STRICT_ANSI__
#include <json/json.h>


static struct tm *today;
static unsigned wday;


static struct connection *new_comic(void)
{
	struct connection *new;

	comics = realloc(comics, (n_comics + 1) * sizeof(struct connection));
	if (!comics) {
		printf("OUT OF MEMORY\n");
		exit(1);
	}

	new = &comics[n_comics];
	++n_comics;

	memset(new, 0, sizeof(struct connection));

	return new;
}


static void add_url(struct connection **conn, char *xurl)
{
	char outurl[1024], *e, *url = (char *)xurl;

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


static void add_days(struct connection **conn, char *days)
{
	unsigned i;

	if (*conn == NULL)
		*conn = new_comic();

	for (i = 0; *days && i < 7; ++days, ++i)
		if (*days != 'X' && *days != 'x')
			(*conn)->days |= 1 << i;
}


static void add_regexp(struct connection **conn, char *regexp)
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


static void add_regmatch(struct connection **conn, int match)
{
	if (*conn == NULL)
		*conn = new_comic();
	if (match >= MATCH_DEPTH)
		printf("<regmatch> %d too big.\n", match);
	else
		(*conn)->regmatch = match;
}


static void add_outname(struct connection **conn, char *outname)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->outname = must_strdup(outname);
}


static void add_base_href(struct connection **conn, char *base_href)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->base_href = must_strdup(base_href);
}


static void add_referer(struct connection **conn, char *referer)
{
	if (*conn == NULL)
		*conn = new_comic();
	if (strcasecmp((char *)referer, "url") == 0 && (*conn)->url)
		(*conn)->referer = must_strdup((*conn)->url);
	else
		(*conn)->referer = must_strdup(referer);
}

/* Helpers */
static char *strval(char *key, struct json_object *val)
{
	if (!json_object_is_type(val, json_type_string)) {
		printf("key %s expected a string\n", key);
		exit(1);
	}

	return (char *)json_object_get_string(val);
}

static int intval(char *key, struct json_object *val)
{
	if (!json_object_is_type(val, json_type_int)) {
		printf("key %s expected an int\n", key);
		exit(1);
	}

	return json_object_get_int(val);
}
/* Helpers */

static void parse_comic(struct json_object *comic)
{
	struct connection *new = NULL;

	json_object_object_foreach(comic, name, val)
		if (strcmp(name, "url") == 0)
			add_url(&new, strval(name, val));
		else if (strcmp(name, "days") == 0)
			add_days(&new, strval(name, val));
		else if (strcmp(name, "regexp") == 0)
			add_regexp(&new, strval(name, val));
		else if (strcmp(name, "regmatch") == 0)
			add_regmatch(&new, intval(name, val));
		else if (strcmp(name, "output") == 0)
			add_outname(&new, strval(name, val));
		else if (strcmp(name, "href") == 0)
			add_base_href(&new, strval(name, val));
		else if (strcmp(name, "referer") == 0)
			add_referer(&new, strval(name, val));
		else
			printf("Unexpected entry %s\n", name);

	/* Sanity */
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
		/* Memory leak and I don't care */
		--n_comics;
	}
}


/* exported */
int read_config(char *fname)
{
	struct json_object *top = NULL;
	time_t now;
	int i, max;

	if (verbose)
		printf("Reading %s\n", fname);

	/* Get the time for the urls */
	time(&now);
	today = localtime(&now);
	wday = 1 << today->tm_wday;

	top = json_object_from_file(fname);
	if (!top) {
		printf("Parse failed\n");
		return 1;
	}

	json_object_object_foreach(top, name, val)
		if (strcmp(name, "comics") == 0) {
			if (!json_object_is_type(val, json_type_array)) {
				printf("Comics must be an array\n");
				exit(1);
			}
			max = json_object_array_length(val);
			for (i = 0; i < max; ++i)
				parse_comic(json_object_array_get_idx(val, i));
		} else if (strcmp(name, "directory") == 0) {
			/* Do not override the command line option */
			if (!comics_dir)
				comics_dir = strval(name, val);
		} else if (strcmp(name, "proxy") == 0)
			set_proxy(strval(name, val));
		else if (strcmp(name, "threads") == 0) {
			if (!threads_set)
				thread_limit = intval(name, val);
		} else if (strcmp(name, "timeout") == 0)
			read_timeout = intval(name, val);
		else if (strcmp(name, "randomize") == 0)
			randomize = intval(name, val);
		else
			printf("Unexpected element '%s'\n", name);

	json_object_put(top);

	return 0;
}


static FILE *wfp;
static struct json_object *top;


static void write_tag(struct json_object *obj, char *tag, char *str)
{
	if (str)
		json_object_object_add(obj, tag, json_object_new_string(str));
}


static void write_int(struct json_object *obj, char *tag, int value)
{
	json_object_object_add(obj, tag, json_object_new_int(value));
}

static void write_comic_trailer(void)
{
	fputs(json_object_to_json_string(top), wfp);
	fputs("\n", wfp);
	fclose(wfp);
	wfp = NULL;
}

/* exported */
void set_failed(char *fname)
{
	wfp = fopen(fname, "w");
	if (wfp == NULL) {
		my_perror(fname);
		exit(1);
	}
}


/* exported */
int write_comic(struct connection *conn)
{
	static struct json_object *comics;
	struct json_object *comic;

	if (!wfp)
		return 0;

	if (!comics) {
		char *proxy;

		top = json_object_new_object();

		write_tag(top, "directory", comics_dir);

		proxy = get_proxy();
		if (proxy) {
			write_tag(top, "proxy", proxy);
			free(proxy);
		}
		write_int(top, "thread_limit", thread_limit);
		write_int(top, "timeout", read_timeout);
		if (randomize)
			write_int(top, "randomize", 1);

		comics = json_object_new_array();
		json_object_object_add(top, "comics", comics);

		atexit(write_comic_trailer);
	}

	comic = json_object_new_object();
	json_object_array_add(comics, comic);

	write_tag(comic, "url", conn->url);
	if (!conn->matched) {
		write_tag(comic, "regexp", conn->regexp);
		if (conn->regmatch)
			write_int(comic, "regmatch", conn->regmatch);
	}
	write_tag(comic, "output", conn->outname);
	write_tag(comic, "href", conn->base_href);
	write_tag(comic, "referer", conn->referer);

	return 0;
}

#endif

/*
 * Local Variables:
 * my-sparse-args: "-DWANT_JSON"
 * End:
 */
