#include "get-comics.h"

#include <event2/event.h>

static CURLM *curlm;

static struct event_base *evbase;
static struct event *timer_event;

static long max_timeout; // SAM DBG

int close_connection(struct connection *conn);

static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct connection *conn = userdata;
	int bytes = size * nmemb;

	printf("write_callback %d\n", bytes); // SAM DBG

	conn->connected = 1;

	int n = write(conn->out, ptr, bytes);
	if (n != bytes)
		printf("Write error\n");

	return n;
}

/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(void)
{
	CURLMsg *msg;
	int msgs_left;
	struct connection *conn;
	CURL *easy;
	CURLcode res;

	printf("check_multi_info\n");

	while ((msg = curl_multi_info_read(curlm, &msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			printf("DONE\n"); // SAM DBG
			easy = msg->easy_handle;
			res = msg->data.result;
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
			if (msg->data.result == 0) {
				if (conn->regexp && !conn->matched)
					process_html(conn);
				else
					close_connection(conn);
			} else
				fail_connection(conn);
		} else
			printf("msg %d\n", msg->msg); // SAM DBG
	}
}

static int socket_callback(CURL *easy,
						   curl_socket_t s,
						   int what,
						   void *userp,
						   void *socketp)
{
	printf("socket_callback\n"); // SAM DBG
			check_multi_info();

	if (what == CURL_POLL_REMOVE) {
		puts("Poll remove");

		if (!start_next_comic() && outstanding == 0) {
			printf("Got all!\n"); // SAM DBG
			event_base_loopbreak(evbase);
		}
	}

	return 0;
}

int timer_callback(CURLM *multi,
				   long timeout_ms,
				   void *userp)
{
	struct timeval timeout;

	printf("timer_callback\n"); // SAM DBG
	if (timeout_ms > max_timeout) max_timeout = timeout_ms;

	if (timeout_ms > 1000) {
		timeout.tv_sec = timeout_ms / 1000;
		timeout_ms -= timeout.tv_sec * 1000;
	} else
		timeout.tv_sec = 0;
	timeout.tv_usec = timeout_ms * 1000;

	evtimer_add(timer_event, &timeout);
	return 0;
}

static void event_timer_callback(int fd, short kind, void *userp)
{
	int running;
	curl_multi_socket_action(curlm, CURL_SOCKET_TIMEOUT, 0, &running);
}

void main_loop(void)
{
	int i;

	if (!(curlm = curl_multi_init()) ||
		curl_multi_setopt(curlm, CURLMOPT_SOCKETFUNCTION, socket_callback) ||
		curl_multi_setopt(curlm, CURLMOPT_TIMERFUNCTION, timer_callback)) {
		printf("Unable to initialize curl\n");
		exit(1);
	}

	if (!(evbase = event_base_new()) ||
		!(timer_event = evtimer_new(evbase, event_timer_callback, NULL))) {
		printf("Unable to initialize libevent\n");
		exit(1);
	}

	/* Setup for the first comics */
	for (i = 0; i < thread_limit; ++i)
		start_next_comic();

	while (start_next_comic() || outstanding)
		event_base_dispatch(evbase);

	printf("curl main loop done\n"); // SAM DBG
	printf("max timeout %ld\n", max_timeout); // SAM DBG
}

int build_request(struct connection *conn)
{
	CURL *curl;
	char *fname;

	printf("build_request %s\n", conn->url); // SAM DBG

	if (!(curl = curl_easy_init())) {
		printf("Unable to create curl context\n");
		return -1;
	}

	conn->curl = curl;

	if (curl_easy_setopt(curl, CURLOPT_URL, conn->url) ||
		curl_easy_setopt(curl, CURLOPT_PRIVATE, conn) ||
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback) ||
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, conn)) {
		printf("Unable to setup curl\n");
		goto failed;
	}

	fname = (conn->regexp && !conn->matched) ? conn->regfname : conn->outname;
	if ((conn->out = creat(fname, 0644)) < 0) {
		my_perror(fname);
		goto failed;
	}

	if (curl_multi_add_handle(curlm, curl)) {
		printf("Unable to add handle to multi handle\n");
		goto failed;
	}

	return 0;

failed:
	conn->curl = NULL;

	if (conn->out >= 0)
		close(conn->out);

	curl_easy_cleanup(curl);
	return -1;
}

#if 0
void check_connect(struct connection *conn)
{
	printf("%s called\n", __func__);
}

void write_request(struct connection *conn)
{
	printf("%s called\n", __func__);
}
#endif

/* This is only for 2 stage comics and redirects */
int release_connection(struct connection *conn)
{
	if (conn->curl) {
		curl_multi_remove_handle(curlm, conn->curl);
		curl_easy_cleanup(conn->curl);
		conn->curl = NULL;
	}

	if (conn->out >= 0) {
		close(conn->out);
		conn->out = -1;
	}

	conn->connected = 0; // SAM needed?

	return 0;
}

int fail_connection(struct connection *conn)
{
	return release_connection(conn);
}

int close_connection(struct connection *conn)
{
	if (conn->curl) {
		++gotit;
		conn->gotit = 1;
		--outstanding;
		if (verbose > 1)
			printf("Closed %s (%d)\n", conn->url, outstanding);
		if (debug_fp)
			fprintf(debug_fp, "%ld:   Closed %3d (%d)\n",
					time(NULL), conn->id, outstanding);
	} else
		printf("Multiple Closes: %s\n", conn->url);
	log_clear(conn);
	return release_connection(conn);
}

/* Reset connection - try again */
int reset_connection(struct connection *conn)
{
	return 0;
}

void set_proxy(char *proxystr)
{
}

void free_cache()
{
	curl_multi_cleanup(curlm);
}
