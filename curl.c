#include "get-comics.h"
#include <pthread.h>

#define MAX_WAIT_MSECS (30 * 1000) /* Wait max. 30 seconds */

#ifndef MULTI_THREADED
static CURLM *curlm;
#endif

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct connection *conn = userdata;
	int bytes = size * nmemb;

	if (conn->out == -1) {
		char *fname;

		if (conn->regexp && !conn->matched)
			fname = conn->regfname;
		else {
			if (want_extensions)
				/* We allocated space for the extension in add_outname */
				strcat(conn->outname, lazy_imgtype(ptr));
			fname = conn->outname;
		}

		if ((conn->out = creat(fname, 0644)) < 0) {
			my_perror(fname);
			fail_connection(conn);
			return bytes;
		}

		conn->connected = 1;
	}

	int n = write(conn->out, ptr, bytes);
	if (n != bytes)
		printf("Write error\n");

	return n;
}

#ifdef MULTI_THREADED
static void *comic_thread(void *arg)
{
	struct connection *conn = arg;
	int http_status_code;

	start_one_comic(conn);

again:
	curl_easy_perform(conn->curl);

	curl_easy_getinfo(conn->curl, CURLINFO_RESPONSE_CODE, &http_status_code);

	if (http_status_code == 200) {
		if (conn->regexp && !conn->matched) {
			if (process_html(conn))
				fail_connection(conn);
			else
				goto again;
		} else
			close_connection(conn);
	} else {
	}

	return NULL;
}

void main_loop(void)
{
	struct connection *conn;

	thread_limit = n_comics;

	for (conn = comics; conn; conn = conn->next)
		if (pthread_create(&conn->thread, NULL, comic_thread, conn))
			printf("Unable to create thread\n");

	for (conn = comics; conn; conn = conn->next)
		pthread_join(conn->thread, NULL);
}
#else
static void msg_done(CURL *curl)
{
	struct connection *conn;
	int http_status_code;

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status_code);
	curl_easy_getinfo(curl, CURLINFO_PRIVATE, &conn);

	if (http_status_code == 200) {
		if (conn->regexp && !conn->matched) {
			if (process_html(conn))
				fail_connection(conn);
		} else
			close_connection(conn);
	} else {
		fprintf(stderr, "GET %s returned %d\n", conn->url, http_status_code);
		fail_connection(conn);
	}
}

void main_loop(void)
{
	int i, running, msgs_left;
	CURLMsg *msg;

	if (curl_global_init(CURL_GLOBAL_DEFAULT) || !(curlm = curl_multi_init())) {
		printf("Unable to initialize curl\n");
		exit(1);
	}

	/* Setup for the first comics */
	for (i = 0; i < thread_limit; ++i)
		start_next_comic();

	curl_multi_perform(curlm, &running);

	while (start_next_comic() || outstanding) {
		int numfds=0;

		curl_multi_wait(curlm, NULL, 0, MAX_WAIT_MSECS, &numfds);

		curl_multi_perform(curlm, &running);

		while ((msg = curl_multi_info_read(curlm, &msgs_left)))
			if (msg->msg == CURLMSG_DONE)
				msg_done(msg->easy_handle);
	}

	curl_multi_cleanup(curlm);
}
#endif

int build_request(struct connection *conn)
{
	if (!(conn->curl = curl_easy_init())) {
		printf("Unable to create curl context\n");
		return -1;
	}

	if (conn->insecure && is_https(conn->url)) {
		/* Skip peer validation and hostname validate. Less secure,
		 * but we are getting comics.
		 */
		curl_easy_setopt(conn->curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(conn->curl, CURLOPT_SSL_VERIFYHOST, 0L);
	}

	curl_easy_setopt(conn->curl, CURLOPT_URL, conn->url);
	curl_easy_setopt(conn->curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(conn->curl, CURLOPT_PRIVATE, conn);
	curl_easy_setopt(conn->curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(conn->curl, CURLOPT_WRITEDATA, conn);
	curl_easy_setopt(conn->curl, CURLOPT_USERAGENT, user_agent);
	/* The empty string tells curl to send all the encodings it supports */
	curl_easy_setopt(conn->curl, CURLOPT_ACCEPT_ENCODING, "");

	/* Implement a read timeout */
	curl_easy_setopt(conn->curl, CURLOPT_LOW_SPEED_LIMIT, 10); /* bps */
	curl_easy_setopt(conn->curl, CURLOPT_LOW_SPEED_TIME, read_timeout);

	if (proxy) {
		curl_easy_setopt(conn->curl, CURLOPT_PROXY, proxy);
		curl_easy_setopt(conn->curl, CURLOPT_PROXYPORT, strtol(proxy_port, NULL, 10));
	}

#ifndef MULTI_THREADED
	if (curl_multi_add_handle(curlm, conn->curl)) {
		printf("Unable to add handle to multi handle\n");
		curl_easy_cleanup(conn->curl);
		conn->curl = NULL;
		return -1;
	}
#endif

	return 0;
}

/* This is only for 2 stage comics and redirects */
int release_connection(struct connection *conn)
{
	if (conn->curl) {
#ifndef MULTI_THREADED
		curl_multi_remove_handle(curlm, conn->curl);
#endif
		curl_easy_cleanup(conn->curl);
		conn->curl = NULL;
	}

	if (conn->out >= 0) {
		close(conn->out);
		conn->out = -1;
	}

	conn->connected = 0;

	return 0;
}

void free_cache(void) {}

char *fixup_url(char *url, char *tmp, int len)
{   /* curl deals with this */
	return url;
}
