#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>


static int in_array;

static void badtag(char *tag)
{
	printf("Invalid tag %s\n", tag);
	exit(1);
}

static char *unesc(char *str)
{
	char ch, *start = str, *out = str;

	while ((ch = *str++)) {
		*out++ = ch;
		if (ch == '\\' && *str == '\\')
			++str;
	}

	*out = '\0';

	/* This is evil... */
	for (str = start; (str = strchr(str, '&')); ) {
		int len = strlen(str) + 1;
		memmove(str + 4, str, len);
		memcpy(str, "&amp;", 5);
		str += 5;
	}

	return start;
}

static void tagout(char *str)
{
	char *e, *p = strchr(++str, '"');
	if (!p)
		badtag(str);

	*p++ = '\0';
	if (*p != ':')
		badtag(str);

	for (++p; isspace(*p); ++p)
		;

	if (*p == '"') {
		/* string */
		++p;
		e = strrchr(p, '"');
		if (!e)
			badtag(str);
		*e = '\0';
	} else if (isdigit(*p)) {
		strtol(p, &e, 0);
		*e = '\0';
	} else if (*p == '[') {
		in_array = 1;
		return;
	} else
		badtag(str);

	if (in_array)
		fputs("  ", stdout);
	printf("<%s>%s</%s>\n", str, unesc(p), str);
}

void outline(char *line)
{
	while (*line)
		if (*line == '/' && *(line + 1) == '*') {
			fputs("<!--", stdout);
			line += 2;
		} else if (*line == '*' && *(line + 1) == '/') {
			fputs("-->", stdout);
			line += 2;
		} else
			putchar(*line++);
	putchar('\n');
}

int main(int argc, char *argv[])
{
	char line[128], *p;

	/* Static header */
	puts("<?xml version=\"1.0\"?>");
	puts("<comics:Configuration xmlns:comics=\"http://seanm.ca/\">");
	puts("");

	/* Parse the top level */
	while (fgets(line, sizeof(line), stdin)) {
		p = strrchr(line, '\n');
		if (p) {
			*p = '\0';
			if (p != line && *(p - 1) == '\r')
				*p = '\0';
		}
		for (p = line; isspace(*p); ++p)
			;
		switch(*p) {
		case '{':
			if (in_array) {
				printf("<comic>");
				if (*++p)
					outline(p);
				else
					putchar('\n');
			}
			break;
		case '}':
			if (in_array)
				printf("</comic>\n");
			break;
		case ']':
			in_array = 0;
			break;
		case '"':
			tagout(p);
			break;
		default:
			outline(line);
		}
	}

	puts("</comics:Configuration>");

	return 0;
}

/*
 * Local Variables:
 * compile-command: "gcc -O3 -Wall -g json2xml.c -o json2xml"
 * End:
 */
