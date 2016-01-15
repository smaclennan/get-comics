/*
 * get-comics.c - download comics from the net
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
#include <dirent.h>

char *comics_dir;
int skipped;

static int unlink_index = 1;

/* If the user specified this on the command line we do not want the
 * config file to override */
int threads_set;


static char *find_regexp(struct connection *conn, char *reg, int regsize)
{
	FILE *fp;
	regex_t regex;
	regmatch_t match[MATCH_DEPTH];
	int err, mn = conn->regmatch;
	/* Max line I have seen is 114k from comics.com! */
	char buf[128 * 1024];

	err = regcomp(&regex, conn->regexp, REG_EXTENDED);
	if (err) {
		char errstr[200];

		regerror(err, &regex, errstr, sizeof(errstr));
		printf("%s\n", errstr);
		regfree(&regex);
		return NULL;
	}

	fp = fopen(conn->regfname, "r");
	if (!fp) {
		my_perror(conn->regfname);
		return NULL;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		if (regexec(&regex, buf, MATCH_DEPTH, match, 0) == 0) {
			/* got a match */
			fclose(fp);
			regfree(&regex);
			if (unlink_index)
				unlink(conn->regfname);

			if (match[mn].rm_so == -1) {
				printf("%s did not have match %d\n",
					   conn->url, mn);
				return NULL;
			}

			*(buf + match[mn].rm_eo) = '\0';
			snprintf(reg, regsize, "%s", buf + match[mn].rm_so);
			return reg;
		}
	}

	if (ferror(fp))
		printf("PROBLEMS\n");

	fclose(fp);

	printf("%s DID NOT MATCH REGEXP\n", conn->url);
	regfree(&regex);

	return NULL;
}

static void add_link(struct connection *conn)
{
	if (verbose)
		printf("Add link %s\n", conn->url);

	fprintf(links_only, "%s\n", conn->url);

	conn->gotit = 1;
	++gotit;
}

int process_html(struct connection *conn)
{
	char imgurl[1024], regmatch[1024], *p;

	if (conn->out >= 0) {
		close(conn->out);
		conn->out = -1;
	}

	p = find_regexp(conn, regmatch, sizeof(regmatch));
	if (p == NULL)
		return 1;

	/* We are done with this socket, but not this connection */
	release_connection(conn);
	free(conn->url);

	if (verbose > 1)
		printf("Matched %s\n", p);

	/* For the writer we need to know if url was modified */
	conn->matched = 1;

	if (is_http(p))
		/* fully rooted */
		conn->url = strdup(p);
	else if (strncmp(p, "//", 2) == 0) {
		/* partially rooted - let's assume http */
		snprintf(imgurl, sizeof(imgurl), "http:%s", p);
		conn->url = strdup(imgurl);
	} else {
		if (conn->base_href)
			snprintf(imgurl, sizeof(imgurl), "%s%s", conn->base_href, p);
		else if (*p == '/')
			snprintf(imgurl, sizeof(imgurl), "%s%s", conn->host, p);
		else
			snprintf(imgurl, sizeof(imgurl), "%s/%s", conn->host, p);
		conn->url = strdup(imgurl);
	}

	if (links_only) {
		add_link(conn);
		--outstanding;
		return 0;
	}

	if (build_request(conn) == 0)
		set_writable(conn);
	else
		printf("build_request %s failed\n", conn->url);

	if (verbose)
		printf("Started %s\n", conn->url);

	return 0;
}


#ifndef WIN32
/* Polarssl can send a sigpipe when a connection is reset by the
 * peer. It is safe to just ignore it.
 */
static void sigpipe(int signum) {}
#endif

static void safe_free(void *mem)
{
	if (mem) free(mem);
}

static void free_comics(void)
{
	while (comics) {
		struct connection *next = comics->next;

		safe_free(comics->url);
		safe_free(comics->host);
		safe_free(comics->regexp);
		safe_free(comics->regfname);
		safe_free(comics->outname);
		safe_free(comics->base_href);
		safe_free(comics->referer);

		free(comics);
		comics = next;
	}

	safe_free(comics_dir);
}

/* We have done a chdir to the comics dir */
static void clean_dir(void)
{
	DIR *dir = opendir(".");
	if (!dir)
		return;

	struct dirent *ent;
	while ((ent = readdir(dir))) {
		if (*ent->d_name == '.') continue;
		char *p = strrchr(ent->d_name, '.');
		if (p && is_imgtype(p))
			unlink(ent->d_name);
		else
			printf("Warning: %s\n", ent->d_name);
	}

	closedir(dir);

	clean_index_dir();
}

static void usage(int rc)
{
	fputs("usage:  get-comics [-hckvCV] [-d comics_dir]", stdout);
	puts(" [-i index_dir] [-l links_file]");
	puts("                   [-p proxy] [-t threads] [-T timeout] [config-file ...]");
	puts("Where:  -h  this help");
	puts("\t-c  clean (remove) images from comics dir before downloading");
	puts("\t-k  keep index files");
	puts("\t-v  verbose");
	puts("\t-C  list supported ciphers (ssl only)");
	puts("\t-V  verify config but don't download comics");
	exit(rc);
}

int main(int argc, char *argv[])
{
	char *env;
	int i, verify = 0, clean = 0;

	while ((i = getopt(argc, argv, "cd:hi:kl:p:t:vCT:V")) != -1)
		switch ((char)i) {
		case 'c':
			clean = 1;
			break;
		case 'd':
			comics_dir = must_strdup(optarg);
			break;
		case 'h':
			usage(0);
		case 'i':
			add_index_dir(optarg);
			break;
		case 'k':
			unlink_index = 0;
			break;
		case 'l':
			links_only = fopen(optarg, "w");
			if (!links_only) {
				my_perror(optarg);
				exit(1);
			}
			break;
		case 'p':
			set_proxy(optarg);
			break;
		case 't':
			thread_limit = strtol(optarg, NULL, 0);
			threads_set = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'C':
#ifdef WANT_SSL
			openssl_list_ciphers();
#else
			puts("-C not supported.");
#endif
			exit(0);

		case 'T':
			read_timeout = strtol(optarg, NULL, 0);
			break;
		case 'V':
			verify = 1;
			break;
		default:
			usage(1);
		}

	if (optind < argc)
		while (optind < argc) {
			if (read_config(argv[optind])) {
				printf("Fatal error in config file\n");
				exit(1);
			}
			++optind;
		}
	else if (read_config(NULL)) {
		printf("Fatal error in config file\n");
		exit(1);
	}

	if (verify) {
		printf("Comics: %u Skipped today: %u\n", n_comics + skipped, skipped);
		if (verbose)
			dump_outstanding(0);
		return 0;
	}

	/* set_proxy will not use this if proxy already set */
	env = getenv("COMICS_PROXY");
	if (env)
		set_proxy(env);

	if (thread_limit == 0) {
		printf("You must allow at least one thread\n");
		exit(1);
	}

	if (thread_limit > n_comics)
		thread_limit = n_comics;

	if (!comics_dir) {
#ifdef WIN32
		char *homedrive = getenv("HOMEDRIVE");
		char *homepath = getenv("HOMEPATH");
		char home[64];
		snprintf(home, sizeof(home), "%s%s", homedrive, homepath);
#else
		char *home = getenv("HOME");
#endif

		if (home) {
			comics_dir = must_alloc(strlen(home) + 10);
			sprintf(comics_dir, "%s/comics", home);
		} else
			comics_dir = must_strdup("comics");
	}

	if (chdir(comics_dir)) {
		my_perror(comics_dir);
		exit(1);
	}

	if (clean)
		clean_dir();

#ifdef _WIN32
	win32_init();
#else
	signal(SIGTERM, dump_outstanding);
	signal(SIGHUP, dump_outstanding);
	signal(SIGPIPE, sigpipe);
#endif

	if (links_only)
		fclose(links_only);

	want_extensions = 1;
	main_loop();

	out_results(comics, skipped);
#ifdef WIN32
	printf("Hit return to exit");
	getchar();
#endif

	free_cache(); /* for valgrind */
	free_comics(); /* for valgrind */
	if (debug_fp)
		fclose(debug_fp);
	return 0;
}
