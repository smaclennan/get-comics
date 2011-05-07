#include "get-comics.h"
#include "js0n.h"

/* Note: res needs to be at least 2 * key pairs * 2 shorts */

static char *strval(unsigned char *str, int len)
{
	static char strbuf[256];

	snprintf(strbuf, sizeof(strbuf), "%.*s", len, str);
	return strbuf;
}

/* js0n returns pointers into the original file, it cannot un-escape
 * the characters. */
static char *unesc(char *str)
{
	char ch, *start = str, *out = str;

	while ((ch = *str++)) {
		*out++ = ch;
		if (ch == '\\' && *str == '\\')
			++str;
	}

	*out = '\0';

	return start;
}

static void parse_comic(unsigned char *comic, int len)
{
	int i;
	unsigned short res[40];
	struct connection *new = NULL;

	memset(res, 0, sizeof(res));
	if (js0n(comic, len, res)) {
		printf("js0n failed parsing comic\n");
		exit(1);
	}

	for (i = 0; res[i] && res[i + 2]; i += 4) {
		char *key = (char *)comic + res[i];
		char *val = strval(comic + res[i + 2], res[i + 3]);

		if (verbose > 1)
			printf("key '%.*s' val '%s'\n", res[i + 1], key, val);

		if (strncmp(key, "url", res[i + 1]) == 0)
			add_url(&new, val);
		else if (strncmp(key, "days", res[i + 1]) == 0)
			add_days(&new, val);
		else if (strncmp(key, "regexp", res[i + 1]) == 0)
			add_regexp(&new, unesc(val));
		else if (strncmp(key, "regmatch", res[i + 1]) == 0)
			add_regmatch(&new, strtol(val, NULL, 0));
		else if (strncmp(key, "output", res[i + 1]) == 0)
			add_outname(&new, val);
		else if (strncmp(key, "href", res[i + 1]) == 0)
			add_base_href(&new, val);
		else if (strncmp(key, "referer", res[i + 1]) == 0)
			add_referer(&new, val);
		else
			printf("Unexpected entry %.*s\n", res[i + 1], key);
	}

	sanity_check_comic(new);
}

static void parse_comics(unsigned char *comics, int len)
{
	int i;
	unsigned short *res;
	char *p = (char *)comics;

	/* Count the comics */
	for (i = 0; (p = strchr(p, '{')); ++i)
		++p;
	if (verbose)
		printf("%d comics\n", i);
	i = (i + 6) * 4; /* the 6 is padding */

	res = calloc(i, sizeof(short));
	if (!res) {
		printf("Out of memory\n");
		exit(1);
	}

	if (js0n(comics, len, res)) {
		printf("js0n failed parsing comics\n");
		exit(1);
	}

	/* At comics level there should only be comic objects */
	for (i = 0; res[i]; i += 2)
		if (*(comics + res[i]) == '{')
			parse_comic(comics + res[i], res[i + 1]);
		else
			printf("Unexpected object %.*s\n",
			       res[i + 1], comics + res[i]);

	free(res);
}

/* exported */
int read_json_config(char *fname)
{
	unsigned char *json = NULL;
	int i, n;
	unsigned short res[40];
	int fd;
	struct stat sbuf;

	if (verbose)
		printf("Reading JSON internal %s\n", fname);

	fd = open(fname, O_RDONLY);
	if (fd < 0 || fstat(fd, &sbuf)) {
		my_perror(fname);
		exit(1);
	}

	json = calloc(1, sbuf.st_size + 1);
	if (!json) {
		printf("Out of memory\n");
		exit(1);
	}

	n = read(fd, json, sbuf.st_size);

	close(fd);

	if (n != sbuf.st_size) {
		printf("Short read %d/%ld\n", n, sbuf.st_size);
		exit(1);
	}

	memset(res, 0, sizeof(res));
	if (js0n(json, n, res)) {
		printf("js0n failed parsing top level\n");
		exit(1);
	}

	/* Parse the top level */
	for (i = 0; res[i] && res[i + 2]; i += 4)
		if (*(json + res[i + 2]) == '[')
			parse_comics(json + res[i + 2], res[i + 3]);
		else {
			char *key = (char *)json + res[i];
			char *val = strval(json + res[i + 2], res[i + 3]);

			if (strncmp(key, "directory", res[i + 1]) == 0) {
				/* Do not override the command line option */
				if (!comics_dir)
					comics_dir = val;
			} else if (strncmp(key, "proxy", res[i + 1]) == 0)
				set_proxy(val);
			else if (strncmp(key, "threads", res[i + 1]) == 0) {
				if (!threads_set)
					thread_limit = strtol(val, NULL, 0);
			} else if (strncmp(key, "timeout", res[i + 1]) == 0)
				read_timeout = strtol(val, NULL, 0);
			else if (strncmp(key, "randomize", res[i + 1]) == 0)
				randomize = strtol(val, NULL, 0);
			else
				printf("Unexpected element '%.*s'\n",
				       res[i + 1], json + res[i]);
		}

	free(json);

	return 0;
}
