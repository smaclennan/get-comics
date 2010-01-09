/*
 * get-comics.c - download comics from the net
 * Copyright (C) 2002,2003 Sean MacLennan <seanm@seanm.ca>
 * $Revision: 1.20 $ $Date: 2004/08/21 18:58:27 $
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

// These are filled in by read_config
char *comics_dir = NULL;
struct connection *comics = NULL;
int n_comics = 0;


struct connection *head = NULL;
struct connection *failed = NULL; // SAM not used yet
int outstanding = 0;

static int gotit = 0;

int unlink_index = 1;
int verbose = 0;
int thread_limit = THREAD_LIMIT;
int randomize = 0;

// If the user specified this on the command line
// we do not want the xml file to override
int threads_set = 0;


static fd_set readfds, writefds;
static int nfds = 0;

static void user_command(void);
static void dump_outstanding(int sig);


char *find_regexp(struct connection *conn)
{
	FILE *fp;
	regex_t regex;
	regmatch_t match[MATCH_DEPTH];
	int err, mn = conn->regmatch;
	/* Max line I have seen is 114k from comics.com! */
	char buf[128 * 1024];

	if((err = regcomp(&regex, conn->regexp, REG_EXTENDED))) {
		char errstr[200];

		regerror(err, &regex, errstr, sizeof(errstr));
		printf("%s\n", errstr);
		regfree(&regex);
		return NULL;
	}

	if(!(fp = fopen(conn->regfname, "r"))) {
		my_perror(conn->regfname);
		return NULL;
	}

	while(fgets(buf, sizeof(buf), fp)) {
		if(regexec(&regex, buf, MATCH_DEPTH, match, 0) == 0) {
			// got a match
			fclose(fp);
			if(unlink_index) unlink(conn->regfname);

			if(match[mn].rm_so == -1) {
				printf("%s matched regexp but did not have match %d\n",
					   conn->url, mn);
				return NULL;
			}

			*(buf + match[mn].rm_eo) = '\0';
			strcpy(conn->buf, buf + match[mn].rm_so);
			return conn->buf;
		}
	}

	if(ferror(fp)) printf("PROBLEMS\n");

	fclose(fp);

	printf("%s DID NOT MATCH REGEXP\n", conn->url);

	return NULL;
}


int start_next_comic()
{
	while(head && outstanding < thread_limit) {
		if(build_request(head) == 0) {
			if(head->sock + 1 > nfds) nfds = head->sock + 1;
			time(&head->access);
			if(verbose) printf("Started %s (%d)\n", head->url, outstanding);
			++outstanding;
			head = head->next;
			return 1;
		}

		printf("build_request %s failed\n", head->url);
		head = head->next;
	}

	return head != NULL;
}


// This is only for 2 stage comics and redirects
int release_connection(struct connection *conn)
{
	if(verbose > 2) printf("Release %s\n", conn->url);

	if(conn->sock != -1) {
		close(conn->sock);
		FD_CLR(conn->sock, &readfds);
		FD_CLR(conn->sock, &writefds);
		if(conn->sock + 1 == nfds) {
			int i;

			for(i = nfds = 0; i < n_comics; ++i)
				if(comics[i].sock > nfds)
					nfds = comics[i].sock;
			if(nfds) ++nfds;
		}
		conn->sock = -1;
	}

	if(conn->out) {
		fclose(conn->out);
		conn->out = NULL;
	}
	conn->func = NULL;

	conn->connected = 0;

	return 0;
}


// Normal way to close connection
int close_connection(struct connection *conn)
{
	if(conn->sock != -1) {
		++gotit;
		--outstanding;
		if(verbose > 1) printf("Closed %s (%d)\n", conn->url, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	return release_connection(conn);
}


// Normal way to close connection
int fail_connection(struct connection *conn)
{
	if(conn->sock != -1) {
		write_comic(conn);
		--outstanding;
		if(verbose > 1) printf("Failed %s (%d)\n", conn->url, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	return release_connection(conn);
}


int process_html(struct connection *conn)
{
	char *p;

	if(conn->out) { fclose(conn->out); conn->out = NULL; }

	if((p = find_regexp(conn)) == NULL) return 1;

	// We are done with this socket, but not this connection
	release_connection(conn);

	if(verbose > 1) printf("Matched %s\n", p);

	// For the writer we need to know if url was modified
	conn->matched = 1;

	if(strncmp(p, "http://", 7) == 0)
		// fully rooted
		conn->url = strdup(p);
	else {
		char imgurl[1024];

		if(conn->base_href)
			sprintf(imgurl, "%s%s", conn->base_href, p);
		else if(*p == '/')
			sprintf(imgurl, "%s%s", conn->host, p);
		else
			sprintf(imgurl, "%s/%s", conn->host, p);
		conn->url = strdup(imgurl);
	}

	if(build_request(conn) == 0) {
		FD_SET(conn->sock, &writefds);
		if(conn->sock + 1 > nfds) nfds = conn->sock + 1;
	} else
		printf("build_request %s failed\n", conn->url);

	if(verbose) printf("Started %s\n", conn->url);

	return 0;
}


int timeout_connections()
{
	int i;
	time_t timeout = time(NULL) - read_timeout;

	for(i = 0; i < n_comics; ++i)
		if(comics[i].sock != -1 && comics[i].access < timeout) {
			printf("TIMEOUT %s\n", comics[i].url);
			fail_connection(&comics[i]);
		}

	return 0;
}


int main(int argc, char *argv[])
{
	char *env;
	int i;
	int n;
	struct timeval timeout, cur_timeout;
	struct connection *conn;

	while((i = getopt(argc, argv, "d:kp:rt:vx:")) != -1)
		switch((char)i) {
		case 'd': comics_dir = optarg; break;
		case 'k': unlink_index = 0; break;
		case 'p': set_proxy(optarg); break;
		case 'r': randomize = 1; break;
		case 't': thread_limit = strtol(optarg, 0, 0); threads_set = 1; break;
		case 'v': verbose++; break;
		case 'x': set_failed(optarg); break;
		case 'h':
		default:
			puts("usage: get-comics [-kv] [-c config]"
				 "[-d comics_dir] [-t threads] "
				 "[-x failed_xml_file] [xml-file]");
			puts("Where: -k  keep index files");
			puts("       -v  verbose");
			exit(1);
		}

	if(optind < argc)
		while(optind < argc) {
			if(read_config(argv[optind])) {
				printf("Fatal error in xml file\n");
				exit(1);
			}
			++optind;
		}
	else if(read_config(XML_FILE)) {
		printf("Fatal error in xml file\n");
		exit(1);
	}

	// Build the linked list - do after randomizing
	for(i = 0; i < n_comics - 1; ++i) comics[i].next = &comics[i + 1];
	head = comics;

	// set_proxy will not use this if proxy already set
	if((env = getenv("COMICS_PROXY"))) set_proxy(env);

	if(thread_limit == 0) {
		printf("You must allow at least one thread\n");
		exit(1);
	}

	if(thread_limit > n_comics) thread_limit = n_comics;

	if(!comics_dir) {
		char *home = getenv("HOME");

		if(home &&
		   (comics_dir = malloc(strlen(home) + 10)))
			sprintf(comics_dir, "%s/comics", home);
		else
			comics_dir = "comics";
	}

	if(chdir(comics_dir)) {
		my_perror(comics_dir);
		exit(1);
	}

#ifdef _WIN32
	win32_init();
#else
	signal(SIGTERM, dump_outstanding);
#endif

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	cur_timeout.tv_sec = 0;
	cur_timeout.tv_usec = 250000;

	// Add stdin
	FD_SET(1, &readfds);

	// start one
	start_next_comic();

	while(head || outstanding) {
		fd_set reads, writes;

		// Windows considers it an error to have reads and writes
		// empty. Make sure we always have a file to process.
		if(outstanding == 0) {
			if(!start_next_comic()) {
				printf("PROBLEMS!\n");
				// head and outstanding should now be null
				continue;
			}
		}

		memcpy(&reads, &readfds, sizeof(reads));
		memcpy(&writes, &writefds, sizeof(writes));
		memcpy(&timeout, &cur_timeout, sizeof(timeout));
		if((n = select(nfds, &reads, &writes, NULL, &timeout)) < 0) {
			my_perror("select");
			continue;
		}

		if(n == 0) {
			if(!start_next_comic()) {
				// Once we have all the comics started, start
				// checking for timeouts. We also increase the timeout
				// period.
				timeout_connections();
				cur_timeout.tv_sec  = 1;
				cur_timeout.tv_usec = 0;
			}
			continue;
		}

		if(FD_ISSET(1, &reads)) user_command();

		for(conn = comics; conn; conn = conn->next) {
			if(conn->sock == -1) continue;
			if(FD_ISSET(conn->sock, &writes)) {
				time(&conn->access);
				write_request(conn);
			} else if(FD_ISSET(conn->sock, &reads)) {
				time(&conn->access);
				if((n = read(conn->sock, conn->curp, conn->rlen)) >= 0) {
					if(verbose > 1) printf("+ Read %d\n", n);
					conn->endp = conn->curp + n;
					conn->rlen -= n;
					*conn->endp = '\0';

					if(conn->func && conn->func(conn))
						fail_connection(conn);
				} else if(n < 0) {
					printf("Read error %s: %d (%d)\n", conn->url, n, errno);
					fail_connection(conn);
				}
			}
		}
	}

	printf("Got %d of %d (%d skipped)\n", gotit, n_comics, skipped);

	return 0;
}

static void dump_outstanding(int sig)
{
	struct connection *conn;
	struct tm *atime;
	time_t now = time(0);

	atime = localtime(&now);
	printf("\nTotal %d Outstanding: %d @ %2d:%02d:%02d\n",
	       n_comics, outstanding, atime->tm_hour, atime->tm_min, atime->tm_sec);
	for(conn = comics; conn; conn = conn->next) {
		if(conn->sock == -1) continue;
		printf("> %s = %s\n", conn->url,
		       conn->connected ? "connected" : "not connected");
		if(conn->regexp)
			printf("  %s %s\n",
			       conn->matched ? "matched" : "regexp", conn->regexp);
		atime = localtime(&conn->access);
		printf("  %2d:%02d:%02d\n",
		       atime->tm_hour, atime->tm_min, atime->tm_sec);
	}
	for(conn = head; conn; conn = conn->next)
		printf("Q %s\n", conn->url);
	fflush(stdout);
}

static void user_command(void)
{
	char buf[80];
	struct connection *conn;

	if(!fgets(buf, sizeof(buf), stdin)) {
		printf("Hmmmmm no user input\n");
		return;
	}

	if(*buf == 'd')
		dump_outstanding(0);
	else if(*buf == 'b') {
		int queued = 0;
		for(conn = head; conn; conn = conn->next) ++queued;
		printf("Total %d Outstanding: %d Queued %d\n",
			   n_comics, outstanding, queued);
	} else
		printf("Unexpected command %s", buf);
}

int set_readable(int sock)
{
	FD_CLR(sock, &writefds);
	FD_SET(sock, &readfds);
	return 0;
}


int set_writable(int sock)
{
	FD_CLR(sock, &readfds);
	FD_SET(sock, &writefds);
	return 0;
}
