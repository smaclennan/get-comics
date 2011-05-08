/*
 * get-comics.c - download comics from the net
 * Copyright (C) 2002-2011 Sean MacLennan <seanm@seanm.ca>
 * Revision: 1.22
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


int read_timeout = SOCKET_TIMEOUT;

/* These are filled in by read_config */
char *comics_dir;
struct connection *comics;
int n_comics;
int skipped;

static struct connection *head;
static int outstanding;

static int gotit;

static int unlink_index = 1;
int verbose;
int thread_limit = THREAD_LIMIT;
int randomize;
static FILE *links_only;

/* If the user specified this on the command line we do not want the
 * conifg file to override */
int threads_set;


static struct pollfd *ufds;
static int npoll;

static void user_command(void);
static void dump_outstanding(int sig);
static void randomize_comics(void);


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
		close(conn->poll->fd);
		conn->poll->fd = -1;
	}
	conn->poll = NULL;

	if (conn->out)
		fclose(conn->out);
	conn->out = NULL;

	conn->func = NULL;

	conn->connected = 0;

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

	if (conn->out) {
		fclose(conn->out);
		conn->out = NULL;
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
	int i;
	time_t timeout = time(NULL) - read_timeout;

	for (i = 0; i < n_comics; ++i)
		if (comics[i].poll && comics[i].access < timeout) {
			printf("TIMEOUT %s\n", comics[i].url);
			fail_connection(&comics[i]);
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
		n = read(conn->poll->fd, conn->curp, conn->rlen);
	if (n >= 0) {
		if (verbose > 1)
			printf("+ Read %d\n", n);
		conn->endp = conn->curp + n;
		conn->rlen -= n;
		*conn->endp = '\0';

		if (conn->func && conn->func(conn))
			fail_connection(conn);
	} else if (n < 0) {
		printf("Read error %s: %d (%d)\n", conn->url, n, errno);
		fail_connection(conn);
	}
}

int main(int argc, char *argv[])
{
	char *env;
	int i, n, timeout = 250, verify = 0;
	struct connection *conn;

	while ((i = getopt(argc, argv, "d:kl:p:rt:vV")) != -1)
		switch ((char)i) {
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
		case 'V':
			verify = 1;
			break;
		case 'h':
		default:
			puts("usage: get-comics [-kv] [-c config]"
				 "[-d comics_dir] [-l links_file] [-t threads] "
				 "[conifg-file]");
			puts("Where: -k  keep index files");
			puts("       -v  verbose");
			exit(1);
		}

	if (optind < argc)
		while (optind < argc) {
			if (read_config(argv[optind])) {
				printf("Fatal error in conifg file\n");
				exit(1);
			}
			++optind;
		}
	else if (read_config(NULL)) {
		printf("Fatal error in conifg file\n");
		exit(1);
	}

	if (verify)
		return 0;

	/* Build the linked list - do after randomizing */
	if (randomize)
		randomize_comics();
	for (i = 0; i < n_comics - 1; ++i)
		comics[i].next = &comics[i + 1];
	head = comics;

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
			comics_dir = malloc(strlen(home) + 10);
			if (!comics_dir) {
				printf("Out of memory\n");
				exit(1);
			}
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

	/* Add stdin */
	ufds[0].fd = 0;
	ufds[0].events = POLLIN;

	/* start one */
	start_next_comic();

	while (head || outstanding) {
		n = poll(ufds, npoll, timeout);
		if (n < 0) {
			my_perror("poll");
			continue;
		}

		if (n == 0) {
			if (!start_next_comic()) {
				/* Once we have all the comics
				 * started, start checking for
				 * timeouts. We also increase the
				 * timeout period. */
				timeout_connections();
				timeout = 1000;
			}
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
				if (!conn->connected)
					check_connect(conn);
				else
					read_conn(conn);
			}
	}

	if (links_only)
		fclose(links_only);

	printf("Got %d of %d (%d skipped)\n", gotit, n_comics, skipped);

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
		printf("Total %d Outstanding: %d Queued %d\n",
			   n_comics, outstanding, queued);
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
			conn->poll->events = POLLIN;
			return 1;
		}

	return 0;
}

static void swap_comics(int i, int n)
{
	struct connection tmp;

	memcpy(&tmp, &comics[n], sizeof(struct connection));
	memcpy(&comics[n], &comics[i], sizeof(struct connection));
	memcpy(&comics[i], &tmp, sizeof(struct connection));
}

static void randomize_comics(void)
{
	int i, n;

	srand((unsigned)time(NULL));

	for (i = 0; i < n_comics; ++i) {
		n = (rand() >> 3) % n_comics;
		if (n != i)
			swap_comics(i, n);
	}
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
