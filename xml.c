#include "get-comics.h"
#include <libxml/xmlmemory.h>


static void parse_comic(xmlDocPtr doc, xmlNodePtr cur)
{
	struct connection *new = NULL;
	xmlNodePtr children;
	char *str;

	for (cur = cur->xmlChildrenNode; cur; cur = cur->next) {
		char *name = (char *)cur->name;

		if (cur->type == XML_COMMENT_NODE)
			continue;
		children = cur->xmlChildrenNode;
		str = (char *)xmlNodeListGetString(doc, children, 1);
		if (strcmp(name, "url") == 0)
			add_url(&new, str);
		else if (strcmp(name, "days") == 0)
			add_days(&new, str);
		else if (strcmp(name, "regexp") == 0)
			add_regexp(&new, str);
		else if (strcmp(name, "regmatch") == 0)
			add_regmatch(&new, strtol(str, NULL, 0));
		else if (strcmp(name, "output") == 0)
			add_outname(&new, str);
		else if (strcmp(name, "href") == 0)
			add_base_href(&new, str);
		else if (strcmp(name, "referer") == 0)
			add_referer(&new, str);
		else
			printf("Unexpected entry %s\n", name);
	}

	sanity_check_comic(new);
}

/* exported */
int read_xml_config(char *fname)
{
	xmlDocPtr  doc;
	xmlNodePtr cur;

	LIBXML_TEST_VERSION;

	xmlKeepBlanksDefault(0); /* ignore indentation */

	doc = xmlParseFile(fname);
	if (!doc) {
		printf("%s: Unable to parse XML file\n", fname);
		return 1;
	}

	cur = xmlDocGetRootElement(doc);
	if (!cur) {
		printf("%s: Empry xml file\n", fname);
		return 1;
	}

	if (!cur->name || strcmp((char *)cur->name, "Configuration")) {
		printf("%s: Root node is not 'Configuration'", fname);
		xmlFreeDoc(doc);
		return 1;
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
				thread_limit = strtol(str, NULL, 0);
		} else if (strcmp(name, "proxy") == 0)
			set_proxy(str);
		else if (strcmp(name, "timeout") == 0)
			read_timeout = strtol(str, NULL, 0);
		else if (strcmp(name, "randomize") == 0)
			randomize = strtol(str, NULL, 0);
		else
			printf("Unexpected element '%s'\n", name);
	}

	xmlFreeDoc(doc);

	return 0;
}
