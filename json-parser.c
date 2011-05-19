#include "get-comics.h"
#include "JSON_parser.h"

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
	else
		printf("Unexpected entry %s\n", key);
}

static void parse_comic_int(char *key, int val)
{
	if (verbose > 1)
		printf("  key '%s' val %d\n", key, val);

	if (strcmp(key, "regmatch") == 0)
		add_regmatch(&new, val);
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

/* exported */
int read_json_config(char *fname)
{
	int count = 0, next_char;
	FILE *input;
	JSON_config config;
	struct JSON_parser_struct *jc;

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
