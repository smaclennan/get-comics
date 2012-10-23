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


int read_timeout = SOCKET_TIMEOUT;

static struct connection *comics;

static struct connection *head;

int thread_limit = THREAD_LIMIT;

static struct pollfd *ufds;
static int npoll;

static void add_url(char *url)
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
		add_url(line);
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

static int start_next_comic(void)
{
	while (head && outstanding < thread_limit)
		if (build_request(head) == 0) {
			time(&head->access);
			if (verbose)
				printf("Started %s (%d)\n",
				       head->url, outstanding);
			++outstanding;
			head = head->next;
			return 1;
		} else {
			printf("build_request %s failed\n", head->url);
			head = head->next;
		}

	return head != NULL;
}


int process_html(struct connection *conn)
{	/* Should never be called with HEAD method */
	printf("Internal Error: %s called\n", __func__);
	fail_connection(conn);
	return 1;
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

static void usage(int rc)
{
	fputs("usage: link-check [-dv] [-p proxy] [-t threads]", stdout);
	puts(" [-T timeout] [link_file ...]");
	exit(rc);
}

int main(int argc, char *argv[])
{
	char *env;
	int i, n, timeout = 250;
	struct connection *conn;

	method = "HEAD";

	while ((i = getopt(argc, argv, "hp:t:vT:")) != -1)
		switch ((char)i) {
		case 'h':
			usage(0);
		case 'p':
			set_proxy(optarg);
			break;
		case 't':
			thread_limit = strtol(optarg, NULL, 0);
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

	if (optind < argc)
		while (optind < argc)
			read_link_file(argv[optind++]);
	else
		read_urls(stdin);

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

#ifdef _WIN32
	win32_init();
#endif

	npoll = thread_limit + 1; /* add one for stdin */
	ufds = must_calloc(npoll, sizeof(struct pollfd));
	for (i = 0; i < npoll; ++i)
		ufds[i].fd = -1;

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

	out_results(comics, 0);

	return n_comics != gotit;
}

int set_conn_socket(struct connection *conn, int sock)
{
	int i;

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
