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


/* I am still not 100% certain this is a good idea. So I am leaving it
 * turned off by default.
#define WANT_RESETS
 */

int read_timeout = SOCKET_TIMEOUT;

char *comics_dir;
int skipped;
static int resets;

static struct connection *comics;
int n_comics;
static int outstanding;
static int gotit;

static struct connection *head;

static int unlink_index = 1;
int verbose;
int thread_limit = THREAD_LIMIT;
int randomize;
static FILE *links_only;

/* If the user specified this on the command line we do not want the
 * config file to override */
int threads_set;


static struct pollfd *ufds;
static int npoll;

static void user_command(void);
static void dump_outstanding(int sig);
static void randomize_comics(void);

/* link-check specific */
static void add_url(char *url)
{
	static struct connection *tail;
	char *e;
	struct connection *conn = must_alloc(sizeof(struct connection));

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

	if (comics)
		tail->next = conn;
	else
		comics = head = conn;
	tail = conn;
	++n_comics;

	printf("host '%s' url '%s'\n", conn->host, conn->url); // SAM DBG
}

/* link-check specific */
int read_link_file(char *fname)
{
	char line[1024];
	FILE *fp = fopen(fname, "r");
	if (!fp) {
		my_perror(fname);
		exit(1);
	}

	while (fgets(line, sizeof(line), fp)) {
		char *p = strchr(line, '\n');
		if (p)
			*p = '\0';

		add_url(line);
	}

	fclose(fp);

	return 0;
}

/* -------------------------------------------------------------- */

static char *find_regexp(struct connection *conn)
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
			if (unlink_index)
				unlink(conn->regfname);

			if (match[mn].rm_so == -1) {
				printf("%s matched regexp but did "
				       "not have match %d\n",
				       conn->url, mn);
				return NULL;
			}

			*(buf + match[mn].rm_eo) = '\0';
			strcpy(conn->buf, buf + match[mn].rm_so);
			return conn->buf;
		}
	}

	if (ferror(fp))
		printf("PROBLEMS\n");

	fclose(fp);

	printf("%s DID NOT MATCH REGEXP\n", conn->url);

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

static int start_next_comic(void)
{
	while (head && outstanding < thread_limit) {
		if (links_only && !head->regexp) {
			add_link(head);
			head = head->next;
			continue;
		} else if (build_request(head) == 0) {
			time(&head->access);
			if (verbose)
				printf("Started %s (%d)\n",
				       head->url, outstanding);
			++outstanding;
			head = head->next;
			return 1;
		}

		printf("build_request %s failed\n", head->url);
		head = head->next;
	}

	return head != NULL;
}


/* This is only for 2 stage comics and redirects */
int release_connection(struct connection *conn)
{
	if (verbose > 2)
		printf("Release %s\n", conn->url);

	openssl_close(conn);

	if (conn->poll && conn->poll->fd != -1) {
		closesocket(conn->poll->fd);
		conn->poll->fd = -1;
	}
	conn->poll = NULL;

	if (conn->out >= 0) {
		close(conn->out);
		conn->out = -1;
	}

	conn->func = NULL;

	conn->connected = 0;

	return 0;
}

/* Reset connection - try again */
int reset_connection(struct connection *conn)
{
#ifdef WANT_RESETS
	++conn->reset;
	if (conn->reset == 1)
		++resets; /* only count each connection once */
	printf("RESET CONNECTION %d: %s\n", conn->reset, conn->url); /* SAM DBG */
	if (conn->reset > 2)
		return fail_connection(conn);

	release_connection(conn);

	if (build_request(conn))
		return fail_connection(conn);
#else
	printf("Read error %s: %d\n", conn->url, errno);
	fail_connection(conn);
#endif

	return 0;
}

/* Normal way to close connection */
int close_connection(struct connection *conn)
{
	if (conn->poll) {
		++gotit;
		conn->gotit = 1;
		--outstanding;
		if (verbose > 1)
			printf("Closed %s (%d)\n", conn->url, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
}


/* Abnormal way to close connection */
int fail_connection(struct connection *conn)
{
	if (conn->poll) {
		write_comic(conn);
		--outstanding;
		if (verbose > 1)
			printf("Failed %s (%d)\n", conn->url, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
}


/* Fail a redirect. We have already released the connection. */
int fail_redirect(struct connection *conn)
{
	if (conn->poll)
		printf("Failed redirect not closed: %s\n", conn->url);
	else {
		write_comic(conn);
		--outstanding;
		if (verbose > 1)
			printf("Failed redirect %s (%d)\n",
			       conn->url, outstanding);
	}
	log_clear(conn);
	return 0;
}

int process_html(struct connection *conn)
{
	char *p;

	if (conn->out >= 0) {
		close(conn->out);
		conn->out = -1;
	}

	p = find_regexp(conn);
	if (p == NULL)
		return 1;

	/* We are done with this socket, but not this connection */
	release_connection(conn);

	if (verbose > 1)
		printf("Matched %s\n", p);

	/* For the writer we need to know if url was modified */
	conn->matched = 1;

	if (is_http(p))
		/* fully rooted */
		conn->url = strdup(p);
	else {
		char imgurl[1024];

		if (conn->base_href)
			sprintf(imgurl, "%s%s", conn->base_href, p);
		else if (*p == '/')
			sprintf(imgurl, "%s%s", conn->host, p);
		else
			sprintf(imgurl, "%s/%s", conn->host, p);
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


static int timeout_connections(void)
{
	struct connection *comic;
	time_t timeout = time(NULL) - read_timeout;

	for (comic = comics; comic; comic = comic->next)
		if (comic->poll && comic->access < timeout) {
			printf("TIMEOUT %s\n", comic->url);
			fail_connection(comic);
		}

	return 0;
}

static void read_conn(struct connection *conn)
{
	int n;

	time(&conn->access);
#ifdef WANT_SSL
	if (conn->ssl) {
		n = openssl_read(conn);
		/* openssl_read can return -EAGAIN if the SSL
		 * connection needs a read or write. */
		if (n == -EAGAIN)
			return;
	} else
#endif
		n = recv(conn->poll->fd, conn->curp, conn->rlen, 0);
	if (n >= 0) {
		if (verbose > 1)
			printf("+ Read %d\n", n);
		conn->endp = conn->curp + n;
		conn->rlen -= n;
		*conn->endp = '\0';

		if (conn->func && conn->func(conn))
			fail_connection(conn);
	} else if (n < 0)
		reset_connection(conn); /* Try again */
}

int main(int argc, char *argv[])
{
	char *env;
	int i, n, timeout = 250, verify = 0;
	struct connection *conn;

	method = "HEAD";

	while ((i = getopt(argc, argv, "c:d:kl:p:rt:vT:V")) != -1)
		switch ((char)i) {
		case 'c':
			read_link_file(optarg);
			break;
		case 'd':
			comics_dir = optarg;
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
		case 'r':
			randomize = 1;
			break;
		case 't':
			thread_limit = strtol(optarg, NULL, 0);
			threads_set = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'T':
			read_timeout = strtol(optarg, NULL, 0);
			break;
		case 'V':
			verify = 1;
			break;
		case 'h':
		default:
			puts("usage: get-comics [-krvV]"
			     "[-d comics_dir] [-l links_file] "
			     "[-p proxy]");
			puts("                  [-t threads] "
			     "[config-file ...]");
			puts("Where: -k  keep index files");
			puts("       -r  randomize");
			puts("       -v  verbose");
			puts("       -V  verify config");
			exit(1);
		}

	if (optind < argc)
		while (optind < argc) {
			add_url(argv[optind]);
			++optind;
		}

	if (randomize)
		randomize_comics();

	if (verify) {
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
		char *home = getenv("HOME");

		if (home) {
			comics_dir = must_alloc(strlen(home) + 10);
			sprintf(comics_dir, "%s/comics", home);
		} else
			comics_dir = "comics";
	}

	if (chdir(comics_dir)) {
		my_perror(comics_dir);
		exit(1);
	}

#ifdef _WIN32
	win32_init();
#else
	signal(SIGTERM, dump_outstanding);
#endif

	npoll = thread_limit + 1; /* add one for stdin */
	ufds = calloc(npoll, sizeof(struct pollfd));
	if (!ufds) {
		printf("Out of poll memory\n");
		exit(1);
	}
	for (i = 0; i < npoll; ++i)
		ufds[i].fd = -1;

#ifndef _WIN32
	/* Add stdin - windows can poll only on sockets */
	if (isatty(0)) {
		ufds[0].fd = 0;
		ufds[0].events = POLLIN;
	}
#endif

	while (head || outstanding) {

		start_next_comic();

		n = poll(ufds, npoll, timeout);
		if (n < 0) {
			my_perror("poll");
			continue;
		}

		if (n == 0) {
			timeout_connections();
			if (!start_next_comic())
				/* Once we have all the comics
				 * started, increase the timeout
				 * period. */
				timeout = 1000;
			continue;
		}

		if (ufds[0].revents)
			user_command();

		for (conn = comics; conn; conn = conn->next)
			if (!conn->poll)
				continue;
			else if (conn->poll->revents & POLLOUT) {
				if (!conn->connected)
					check_connect(conn);
				else {
					time(&conn->access);
					write_request(conn);
				}
			} else if (conn->poll->revents & POLLIN) {
				/* This check is needed for openssl */
				if (!conn->connected)
					check_connect(conn);
				else
					read_conn(conn);
			}
	}

	if (links_only)
		fclose(links_only);

	printf("Got %d of %d (%d skipped)\n", gotit, n_comics, skipped);
	if (resets)
		printf("\t%d reset\n", resets);

	/* Dump the missed comics */
	for (conn = comics; conn; conn = conn->next)
		if (!conn->gotit) {
			if (conn->outname)
				printf("  %s (%s)\n", conn->url, conn->outname);
			else
				printf("  %s\n", conn->url);
		}

	return 0;
}

static void dump_outstanding(int sig)
{
	struct connection *conn;
	struct tm *atime;
	time_t now = time(NULL);

	atime = localtime(&now);
	printf("\nTotal %d Outstanding: %d @ %2d:%02d:%02d\n",
	       n_comics, outstanding,
	       atime->tm_hour, atime->tm_min, atime->tm_sec);
	for (conn = comics; conn; conn = conn->next) {
		if (!conn->poll)
			continue;
		printf("> %s = %s\n", conn->url,
		       conn->connected ? "connected" : "not connected");
		if (conn->regexp)
			printf("  %s %s\n",
			       conn->matched ? "matched" : "regexp",
			       conn->regexp);
		atime = localtime(&conn->access);
		printf("  %2d:%02d:%02d\n",
		       atime->tm_hour, atime->tm_min, atime->tm_sec);
	}
	for (conn = head; conn; conn = conn->next)
		printf("Q %s\n", conn->url);
	fflush(stdout);
}

static void user_command(void)
{
	char buf[80], *p;
	struct connection *conn;

	if (!fgets(buf, sizeof(buf), stdin)) {
		printf("Hmmmmm no user input\n");
		return;
	}

	for (p = buf; isspace(*p); ++p)
		;

	if (*p == 'd')
		dump_outstanding(0);
	else if (*p == 'b') {
		int queued = 0;
		for (conn = head; conn; conn = conn->next)
			++queued;
		printf("Total %d Got %d Outstanding %d Queued %d\n",
		       n_comics, gotit, outstanding, queued);
	} else if (*p)
		printf("Unexpected command %s", buf);
}

int set_conn_socket(struct connection *conn, int sock)
{
	int i;

	if (conn->poll) { /* SAM DBG */
		printf("PROBLEMS! conn->poll set!\n");
		return 0;
	}

	for (i = 1; i < npoll; ++i)
		if (ufds[i].fd == -1) {
			conn->poll = &ufds[i];
			conn->poll->fd = sock;
			/* All sockets start out writable */
			conn->poll->events = POLLOUT;
			return 1;
		}

	return 0;
}

void add_comic(struct connection *new)
{
	static struct connection *tail;

	if (comics)
		tail->next = new;
	else
		comics = head = new;
	tail = new;
	++n_comics;
}

static void randomize_comics(void)
{
	struct connection **array, *tmp;
	int i, n;

	srand((unsigned)time(NULL));

	array = calloc(n_comics, sizeof(struct connection *));
	if (!array) {
		printf("Out of memory\n");
		exit(1);
	}

	for (i = 0, tmp = comics; tmp; tmp = tmp->next, ++i)
		array[i] = tmp;

	for (i = 0; i < n_comics; ++i) {
		n = (rand() >> 3) % n_comics;
		tmp = array[i];
		array[i] = array[n];
		array[n] = tmp;
	}

	for (i = 0; i < n_comics - 1; ++i)
		array[i]->next = array[i + 1];
	array[i]->next = NULL;

	comics = head = array[0];

	free(array);
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

void *must_alloc(int size)
{
	void *new = calloc(1, size);
	if (!new) {
		printf("OUT OF MEMORY\n");
		exit(1);
	}
	return new;
}

/* This is a very lazy checking heuristic since we expect the files to
 * be one of the four formats and well formed. Yes, Close To Home
 * actually used TIFF. TIFF is only tested on little endian machine. */
char *lazy_imgtype(struct connection *conn)
{
	static struct header {
		char *ext;
		unsigned char hdr[4];
	} hdrs[] = {
		{ ".gif", { 'G', 'I', 'F', '8' } }, /* gif89 and gif87a */
		{ ".jpg", { 0xff, 0xd8, 0xff, 0xe0 } }, /* jfif */
		{ ".jpg", { 0xff, 0xd8, 0xff, 0xe1 } }, /* exif */
		{ ".png", { 0x89, 'P', 'N', 'G' } },
		{ ".tif", { 'M', 'M', 0, 42 } }, /* big endian */
		{ ".tif", { 'I', 'I', 42, 0 } }, /* little endian */
	};
	int i;

	for (i = 0; i < sizeof(hdrs) / sizeof(struct header); ++i)
		if (memcmp(conn->curp, hdrs[i].hdr, 4) == 0)
			return hdrs[i].ext;

	printf("WARNING: Unknown file type %s\n", conn->outname);
	return ".xxx";
}