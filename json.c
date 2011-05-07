#include "get-comics.h"

#define __STRICT_ANSI__
#include <json/json.h>


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

	sanity_check_comic(new);
}

/* exported */
int read_json_config(char *fname)
{
	struct json_object *top = NULL;
	int i, max;

	if (verbose)
		printf("Reading JSON %s\n", fname);

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
