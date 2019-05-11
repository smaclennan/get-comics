/*
 * link-check.c - check a list of urls
 * Copyright (C) 2002-2011 Sean MacLennan <seanm@seanm.ca>
 * Revision: 1.3
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this project; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "get-comics.h"
#include <regex.h>
#include <getopt.h>
#include <signal.h>


static char *regexp;
static int regmatch;
static int create_unique;

static char *unique_outname(void)
{
	static unsigned unique;
	char out[24], *p;

	snprintf(out, sizeof(out), "out%03u", ++unique);
	p = must_alloc(strlen(out) + 1 + 4);
	want_extensions = 1;
	return strcpy(p, out);
}

static void add_get_url(char *url)
{
	static struct connection *tail;
	char *e;
	struct connection *conn = must_alloc(sizeof(struct connection));

	/* Hack for BME files. */
	e = strchr(url, ' ');
	if (e)
		*e = '\0';

	conn->out = -1;
	conn->url = must_strdup(url);

	/* isolate the host from original url */
	e = is_http(url);
	if (e)
		e = strchr(e, '/');
	else
		e = strchr(url + 1, '/');
	if (e)
		*e = '\0';

	conn->host = must_strdup(url);

	if (create_unique)
		conn->outname = unique_outname();
	else
		conn->outname = create_outname(url);

	if (regexp) {
		do_add_regexp(conn, regexp, NULL);
		conn->regmatch = regmatch;
	}

	if (comics)
		tail->next = conn;
	else
		comics = head = conn;
	tail = conn;
	++n_comics;
}

static void read_urls(FILE *fp)
{
	char line[1024], *p;

	while (fgets(line, sizeof(line), fp)) {
		p = strrchr(line, '\n');
		if (p)
			*p = '\0';
		add_get_url(line);
	}
}

static int read_link_file(char *fname)
{
	FILE *fp = fopen(fname, "r");
	if (fp) {
		read_urls(fp);
		fclose(fp);
	} else
		my_perror(fname);

	return 0;
}

static void usage(int rc)
{
	fputs("usage: http-get [-v] [-t threads]", stdout);
	puts(" [-T timeout] [link_file ...]");
	puts("\nIf no link_files are specified, read urls from stdin.");
	exit(rc);
}


int main(int argc, char *argv[])
{
	int i;

	while ((i = getopt(argc, argv, "hl:r:R:t:uUvT:")) != -1)
		switch ((char)i) {
		case 'h':
			usage(0);
		case 'l':
			read_link_file(optarg);
			break;
		case 'r':
			regexp = optarg;
			break;
		case 'R':
			regmatch = strtol(optarg, NULL, 0);
			if (regmatch >= MATCH_DEPTH)
				printf("-R %d >= %d.\n", regmatch, MATCH_DEPTH);
			break;
		case 't':
			thread_limit = strtol(optarg, NULL, 0);
			break;
		case 'u':
			unlink_index = 0;
			break;
		case 'U':
			create_unique = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'T':
			read_timeout = strtol(optarg, NULL, 0);
			break;
		default:
			usage(1);
		}

	while (optind < argc)
		add_get_url(argv[optind++]);

	if (thread_limit == 0) {
		printf("You must allow at least one thread\n");
		exit(1);
	}

	if (thread_limit > n_comics)
		thread_limit = n_comics;

#ifdef _WIN32
	win32_init();
#endif

	main_loop();

	out_results(comics, 0);

	return n_comics != gotit;
}

