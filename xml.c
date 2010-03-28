#include "get-comics.h"
#include <libxml/xmlmemory.h>


#ifdef _WIN32
#define strcasecmp stricmp
#endif


static struct tm *today;
static unsigned wday;

int run_m4;
int skipped;
int write_failed = 1;


static void randomize_comics(void);

static struct connection *new_comic()
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


char *must_strdup(char *old)
{
	char *new = strdup(old);
	if (!new) {
		printf("OUT OF MEMORY\n");
		exit(1);
	}
	return new;
}

static char *must_xml_strdup(xmlChar *old)
{
	return must_strdup((char *)old);
}


static void add_url(struct connection **conn, xmlChar *xurl)
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
		if (strncmp(url, "http://", 7) == 0)
			e = strchr(url + 7, '/');
		else
			e = strchr(url + 1, '/');
		if (e)
			*e = '\0';

		(*conn)->host = must_strdup(url);
	}
}


static void add_days(struct connection **conn, xmlChar *days)
{
	unsigned i;

	if (*conn == NULL)
		*conn = new_comic();

	for (i = 0; *days && i < 7; ++days, ++i)
		if (*days != 'X' && *days != 'x')
			(*conn)->days |= 1 << i;
}


static void add_regexp(struct connection **conn, xmlChar *regexp)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->regexp = must_xml_strdup(regexp);
	if ((*conn)->regfname == NULL) {
		char fname[20];

		sprintf(fname, "index-%08x.html", n_comics);
		(*conn)->regfname = must_strdup(fname);
	}
}


static void add_regmatch(struct connection **conn, xmlChar *regmatch)
{
	int match;

	if (*conn == NULL)
		*conn = new_comic();
	match = strtol((char *)regmatch, 0, 0);
	if (match >= MATCH_DEPTH)
		printf("<regmatch> %d too big.\n", match);
	else
		(*conn)->regmatch = match;
}


static void add_outname(struct connection **conn, xmlChar *outname)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->outname = must_xml_strdup(outname);
}


static void add_base_href(struct connection **conn, xmlChar *base_href)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->base_href = must_xml_strdup(base_href);
}


static void add_referer(struct connection **conn, xmlChar *referer)
{
	if (*conn == NULL)
		*conn = new_comic();
	if (strcasecmp((char *)referer, "url") == 0 && (*conn)->url)
		(*conn)->referer = must_strdup((*conn)->url);
	else
		(*conn)->referer = must_xml_strdup(referer);
}


static void add_optional(struct connection **conn, xmlChar *optional)
{
	if (*conn == NULL)
		*conn = new_comic();
	(*conn)->optional = strtol((char *)optional, 0, 0);
}


static void parse_comic(xmlDocPtr doc, xmlNodePtr cur)
{
	struct connection *new = NULL;
	xmlNodePtr children;
	xmlChar *str;

	for (cur = cur->xmlChildrenNode; cur; cur = cur->next) {
		char *name = (char *)cur->name;

		if (cur->type == XML_COMMENT_NODE)
			continue;
		children = cur->xmlChildrenNode;
		str = xmlNodeListGetString(doc, children, 1);
		if (strcmp(name, "url") == 0)
			add_url(&new, str);
		else if (strcmp(name, "days") == 0)
			add_days(&new, str);
		else if (strcmp(name, "regexp") == 0)
			add_regexp(&new, str);
		else if (strcmp(name, "regmatch") == 0)
			add_regmatch(&new, str);
		else if (strcmp(name, "output") == 0)
			add_outname(&new, str);
		else if (strcmp(name, "href") == 0)
			add_base_href(&new, str);
		else if (strcmp(name, "referer") == 0)
			add_referer(&new, str);
		else if (strcmp(name, "optional") == 0)
			add_optional(&new, str);
		else
			printf("Unexpected entry %s\n", name);
	}

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
	xmlDocPtr  doc;
	xmlNodePtr cur;
	time_t now;
	char tmpname[20];

	if (verbose)
		printf("Reading %s\n", fname);

#ifndef _WIN32
	if (run_m4) {
		FILE *in;
		int out, n, len;
		char line[1024];

		strcpy(tmpname, "/tmp/comicsXXXXXX");
		out = mkstemp(tmpname);
		if (out < 0) {
			my_perror("mkstemp");
			return 1;
		}

		sprintf(line, "m4 %.200s", fname);
		in = popen(line, "r");
		if (in == NULL) {
			my_perror(line);
			return 1;
		}

		while (fgets(line, sizeof(line), in)) {
			len = strlen(line);
			n = write(out, line, len);
			if (n != len) {
				printf("m4 processing failed on output\n");
				return 1;
			}
		}

		if (ferror(in)) {
			printf("m4 processing failed on input\n");
			return 1;
		}

		close(out);
		pclose(in);

		fname = tmpname;
	}
#endif

	LIBXML_TEST_VERSION;

	/* Get the time for the urls */
	time(&now);
	today = localtime(&now);
	wday = 1 << today->tm_wday;

	xmlKeepBlanksDefault(0); /* ignore indentation */

	doc = xmlParseFile(fname);
	if (!doc) {
		printf("%s: Unable to parse XML file\n", fname);
		goto failed;
	}

	cur = xmlDocGetRootElement(doc);
	if (!cur) {
		printf("%s: Empry xml file\n", fname);
		goto failed;
	}

	if (!cur->name || strcmp((char *)cur->name, "Configuration")) {
		printf("%s: Root node is not 'Configuration'", fname);
		xmlFreeDoc(doc);
		goto failed;
	}

	for (cur = cur->xmlChildrenNode; cur; cur = cur->next) {
		char *name = (char *)cur->name;
		char *str = (char *)xmlNodeListGetString(doc,
							cur->xmlChildrenNode,
							1);

		if (cur->type == XML_COMMENT_NODE)
			continue;

		if (strcmp(name, "comic") == 0)
			parse_comic(doc, cur);
		else if (strcmp(name, "directory") == 0) {
			/* Do not override the command line option */
			if (!comics_dir)
				comics_dir = must_strdup(str);
		} else if (strcmp(name, "threads") == 0) {
			if (!threads_set)
				thread_limit = strtol(str, 0, 0);
		} else if (strcmp(name, "proxy") == 0)
			set_proxy(str);
		else if (strcmp(name, "timeout") == 0)
			read_timeout = strtol(str, 0, 0);
		else if (strcmp(name, "randomize") == 0)
			randomize = strtol(str, 0, 0);
		else if (strcmp(name, "write-failed") == 0)
			write_failed = strtol(str, 0, 0);
		else
			printf("Unexpected element '%s'\n", name);
	}

	xmlFreeDoc(doc);

	if (randomize)
		randomize_comics();

	if (run_m4)
		unlink(tmpname);
	return 0;

failed:
	if (run_m4)
		unlink(tmpname);
	return 1;
}


static FILE *wfp;


inline int write_tag(char *tag, char *value)
{
	if (value)
		fprintf(wfp, "  <%s>%s</%s>\n", tag, value, tag);
	return 0;
}


static void write_comic_trailer(void)
{
	if (wfp) {
		fputs("\n</comics:Configuration>\n", wfp);
		fclose(wfp);
		wfp = NULL;
	}
}


void set_failed(char *fname)
{
	wfp = fopen(fname, "w");
	if (wfp == NULL) {
		my_perror(fname);
		exit(1);
	}
}


int write_comic(struct connection *conn)
{
	static int first_time = 1;

	if (!wfp)
		return 0;

	if (!write_failed)
		return 0;

	if (first_time) {
		/* write the global values */
		char *proxy;

		first_time = 0;

		fputs("<?xml version=\"1.0\"?>\n"
		      "<comics:Configuration xmlns:"
		      "comics=\"http://seanm.ca/\">\n\n", wfp);

		fprintf(wfp, "<directory>%s</directory>\n", comics_dir);
		proxy = get_proxy();
		if (proxy) {
			fprintf(wfp, "<proxy>%s</proxy>\n", proxy);
			free(proxy);
		}
		fprintf(wfp, "<threads>%d</threads>\n", thread_limit);
		fprintf(wfp, "<timeout>%d</timeout>\n", read_timeout);
		if (randomize)
			fputs("<randomize>1</randomize>\n", wfp);
		fputc('\n', wfp);

		atexit(write_comic_trailer);
	}

	fputs("<comic>\n", wfp);
	write_tag("url", conn->url);
	if (!conn->matched) {
		/* We have already updated the url correctly */
		write_tag("regexp", conn->regexp);
		if (conn->regmatch)
			fprintf(wfp, "  <regmatch>%d</regmatch>\n",
				conn->regmatch);
	}
	write_tag("output", conn->outname);
	write_tag("href", conn->base_href);
	write_tag("referer", conn->referer);
	/* bitmask not needed? */
	if (conn->optional)
		fputs("  <optional>1</optional>\n", wfp);
	fputs("</comic>\n", wfp);

	return 0;
}



/* Send to stdout, not stderr */
void my_perror(char *str)
{
#ifdef _WIN32
	/* SAM FIX */
	printf("%s: error %d\n", str, WSAGetLastError());
#else
	printf("%s: %s\n", str, strerror(errno));
#endif
}


static void swap_comics(int i, int n)
{
	struct connection tmp;

	memcpy(&tmp, &comics[n], sizeof(struct connection));
	memcpy(&comics[n], &comics[i], sizeof(struct connection));
	memcpy(&comics[i], &tmp, sizeof(struct connection));
}

void randomize_comics(void)
{
	int i, n;

	srand((unsigned)time(0));

	for (i = 0; i < n_comics; ++i) {
		n = (rand() >> 3) % n_comics;
		if (n != i)
			swap_comics(i, n);
	}
}
