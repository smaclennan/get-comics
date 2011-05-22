#include "get-comics.h"

#ifdef LOGGING
#include <stdarg.h>

#define EVENT_INC 32

static struct log *get_log(struct connection *conn)
{
	if (!conn->log)
		conn->log = must_alloc(sizeof(struct log));

	return conn->log;
}

/* We do not need any locking since get-comics is single threaded */
void log_add(struct connection *conn, char *fmt, ...)
{
	char txt[128];
	struct log *log = get_log(conn);
	va_list ap;

	if (log->n_events >= log->max_events) {
		char **new;

		log->max_events += EVENT_INC;
		new = realloc(log->events, sizeof(char *) * log->max_events);
		if (!new) {
			printf("Unable to allocate more log events\n");
			log->failed |= 1;
			return;
		}

		log->events = new;
	}

	/* SAM timestamp? */

	va_start(ap, fmt);
	vsnprintf(txt, sizeof(txt), fmt, ap);
	va_end(ap);

	log->events[log->n_events] = strdup(txt);
	if (!log->events[log->n_events]) {
		printf("Unable to allocate log string\n");
		log->failed |= 1;
		return;
	}

	++log->n_events;
}

void log_want_dump(struct connection *conn)
{	/* User wants log to dump */
	struct log *log = get_log(conn);
	log->failed |= 2;
}

void log_dump(struct connection *conn)
{
	int i;

	if (!conn->log)
		return; /* nothing to see */

	printf(">Log dump %s\n", conn->url);
	if (conn->log->failed & 1)
		printf(">WARNING: Log entries incomplete\n");
	for (i = 0; i < conn->log->n_events; ++i)
		puts(conn->log->events[i]);
	puts("> EOD");
}

void log_clear(struct connection *conn)
{
	int i;

	if (!conn->log)
		return; /* that was easy */

	if (!conn->log->n_events)
		return;

	if (conn->log->failed & 2)
		log_dump(conn);

	for (i = 0; i < conn->log->n_events; ++i)
		free(conn->log->events[i]);
	free(conn->log->events);
	conn->log->n_events = 0;
	conn->log->max_events = 0;
}
#else
void log_clear(struct connection *conn) {}
#endif
