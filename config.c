#include "get-comics.h"
#include "my-parser.h"

static struct tm *today;
static unsigned wday;
static char *gocomics_regexp;

static void new_comic(struct connection **conn)
{
	if (*conn == NULL) {
		*conn = must_alloc(sizeof(struct connection));
		(*conn)->out = -1;
	}
}

static void sanity_check_comic(struct connection *new)
{
	if (!new)
		/* Empty entries are allowed */
		return;
	else if (!new->url) {
		printf("ERROR: comic with no url!\n");
		exit(1);
	} else if (!new->host) {
		printf("ERROR: comic with no host!\n");
		exit(1);
	} else if (new->days && (new->days & wday) == 0) {
		if (verbose)
			printf("Skipping: %s\n", new->url);
		++skipped;
		free(new);
	} else if (!is_http(new->url)) {
		if (verbose)
			printf("Skipping not http: %s\n", new->url);
		++skipped;
		free(new);
	} else
		add_comic(new);
}

static void add_url(struct connection **conn, char *url)
{
	char outurl[1024], *e;

	new_comic(conn);

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

	new_comic(conn);

	for (i = 0; *days && i < 7; ++days, ++i)
		if (*days != 'X' && *days != 'x')
			(*conn)->days |= 1 << i;
}

static void add_regexp(struct connection **conn, char *regexp)
{
	static int unique;
	char out[256];

	if (strftime(out, sizeof(out), regexp, today) == 0) {
		printf("strftime failed for '%s'\n", regexp);
		exit(1);
	}

	new_comic(conn);
	(*conn)->regexp = must_strdup(out);
	if ((*conn)->regfname == NULL) {
		sprintf(out, "index-%08x.html", ++unique);
		(*conn)->regfname = must_strdup(out);
	}
}

static void add_regmatch(struct connection **conn, int match)
{
	new_comic(conn);
	if (match >= MATCH_DEPTH)
		printf("<regmatch> %d too big.\n", match);
	else
		(*conn)->regmatch = match;
}

static void add_outname(struct connection **conn, char *outname)
{
	new_comic(conn);
	(*conn)->outname = must_alloc(strlen(outname) + 4 + 1);
	strcpy((*conn)->outname, outname);
}

static void add_base_href(struct connection **conn, char *base_href)
{
	new_comic(conn);
	(*conn)->base_href = must_strdup(base_href);
}

static void add_referer(struct connection **conn, char *referer)
{
	new_comic(conn);
	if (strcasecmp((char *)referer, "url") == 0 && (*conn)->url)
		(*conn)->referer = must_strdup((*conn)->url);
	else
		(*conn)->referer = must_strdup(referer);
}

static void add_gocomic(struct connection **conn, char *comic)
{
	char url[1024];

	if (!gocomics_regexp) {
		puts("ERROR: gocomic entry but gocomics-regexp not set");
		exit(1);
	}

	snprintf(url, sizeof(url), "http://www.gocomics.com/%s/", comic);
	add_url(conn, url);
	add_regexp(conn, gocomics_regexp);
	add_outname(conn, comic);
}

static void add_redirect(struct connection **conn, int val)
{
	new_comic(conn);
	(*conn)->redirect_ok = val;
}

static char s_key[80];
static int s_iskey;

static struct connection *new;
static int in_comics;

static void parse_top_str(char *key, char *val)
{
	if (verbose > 1)
		printf("key '%s' val '%s'\n", key, val);

	if (strcmp(key, "directory") == 0) {
		/* Do not override the command line option */
		if (!comics_dir)
			comics_dir = strdup(val);
	} else if (strcmp(key, "proxy") == 0)
		set_proxy(val);
	else if (strcmp(key, "gocomics-regexp") == 0)
		gocomics_regexp = must_strdup(val);
	else
		printf("Unexpected element '%s'\n", key);
}

static void parse_top_int(char *key, int val)
{
	if (verbose > 1)
		printf("key '%s' val %d\n", key, val);

	if (strcmp(key, "threads") == 0) {
		if (!threads_set)
			thread_limit = val;
	} else if (strcmp(key, "timeout") == 0)
		read_timeout = val;
	else if (strcmp(key, "randomize") == 0)
		randomize = val;
	else
		printf("Unexpected element '%s'\n", key);
}

static void parse_comic_str(char *key, char *val)
{
	if (verbose > 1)
		printf("  key '%s' val '%s'\n", key, val);

	if (strcmp(key, "url") == 0)
		add_url(&new, val);
	else if (strcmp(key, "days") == 0)
		add_days(&new, val);
	else if (strcmp(key, "regexp") == 0)
		add_regexp(&new, val);
	else if (strcmp(key, "output") == 0)
		add_outname(&new, val);
	else if (strcmp(key, "href") == 0)
		add_base_href(&new, val);
	else if (strcmp(key, "referer") == 0)
		add_referer(&new, val);
	else if (strcmp(key, "gocomic") == 0)
		add_gocomic(&new, val);
	else
		printf("Unexpected entry %s\n", key);
}

static void parse_comic_int(char *key, int val)
{
	if (verbose > 1)
		printf("  key '%s' val %d\n", key, val);

	if (strcmp(key, "regmatch") == 0)
		add_regmatch(&new, val);
	else if (strcmp(key, "redirect") == 0)
		add_redirect(&new, val);
	else
		printf("Unexpected entry %s\n", key);
}

static int parse(void *ctx, int type, const JSON_value *value)
{
	switch (type) {
	case JSON_T_KEY:
		s_iskey = 1;
		snprintf(s_key, sizeof(s_key), "%s", value->vu.str.value);
		return 1; /* do not break */

	case JSON_T_STRING:
		if (!s_iskey) {
			printf("Parse error: string with no key\n");
			exit(1);
		}
		if (in_comics)
			parse_comic_str(s_key, (char *)value->vu.str.value);
		else
			parse_top_str(s_key, (char *)value->vu.str.value);
		break;

	case JSON_T_INTEGER:
		if (!s_iskey) {
			printf("Parse error: int with no key\n");
			exit(1);
		}
		if (in_comics)
			parse_comic_int(s_key, value->vu.integer_value);
		else
			parse_top_int(s_key, value->vu.integer_value);
		break;

	case JSON_T_ARRAY_BEGIN:
		if (s_iskey && strcmp(s_key, "comics") == 0)
			in_comics = 1;
		else {
			printf("Invalid array\n");
			exit(1);
		}
		break;

	case JSON_T_ARRAY_END:
		in_comics = 0;
		break;

	case JSON_T_OBJECT_BEGIN:
		if (in_comics) {
			new = NULL;
			if (verbose > 1)
				printf("Comic:\n");
		}
		break;

	case JSON_T_OBJECT_END:
		if (in_comics)
			sanity_check_comic(new);
		break;

	default:
		printf("Unexpected JSON object %d\n", type);
	}

	s_iskey = 0;
	return 1;
}

int read_config(char *fname)
{
	int count = 0, next_char;
	FILE *input;
	JSON_config config;
	JSON_parser jc;
	time_t now;

	if (!fname)
		fname = JSON_FILE;

	/* Get the time for the urls */
	time(&now);
	today = localtime(&now);
	wday = 1 << today->tm_wday;

	init_JSON_config(&config);

	config.depth = 3;
	config.callback = parse;
	config.allow_comments = 1;

	jc = new_JSON_parser(&config);

	input = fopen(fname, "r");
	if (!input) {
		my_perror(fname);
		exit(1);
	}

	while ((next_char = fgetc(input)) > 0) {
		++count;
		if (!JSON_parser_char(jc, next_char)) {
			printf("JSON_parser: syntax error byte %d\n", count);
			exit(1);
		}
	}

	fclose(input);

	if (!JSON_parser_done(jc)) {
		printf("JSON_parser: unexpected EOF\n");
		exit(1);
	}

	delete_JSON_parser(jc);
	return 0;
}
