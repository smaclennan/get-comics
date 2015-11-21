#include "get-comics.h"

int build_request(struct connection *conn)
{
	return -1;
}

void check_connect(struct connection *conn)
{
}

void write_request(struct connection *conn)
{
}

/* This is only for 2 stage comics and redirects */
int release_connection(struct connection *conn)
{
	return 0;
}

int fail_connection(struct connection *conn)
{
	return 0;
}

/* Reset connection - try again */
int reset_connection(struct connection *conn)
{
	return 0;
}

void set_proxy(char *proxystr)
{
}

void free_cache() {}
