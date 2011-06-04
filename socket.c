#include "get-comics.h"
#include <fcntl.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif


/* Only define this if your OS does not support getaddrinfo.
#define IPV4
*/

static int tcp_connected(struct connection *conn)
{
#ifdef WANT_SSL
	if (is_https(conn->url)) {
		if (openssl_connect(conn)) {
			fail_connection(conn);
			return -1;
		}
	} else
#endif
		conn->connected = 1;

	return 0;
}

static int set_non_blocking(int sock)
{
#ifdef _WIN32
	u_long optval = 1;
	if (ioctlsocket(sock, FIONBIO, &optval)) {
		printf("ioctlsocket FIONBIO failed\n");
		close(sock);
		return -1;
	}
#else
	int optval = fcntl(sock, F_GETFL, 0);
	if (optval == -1 || fcntl(sock, F_SETFL, optval | O_NONBLOCK)) {
		my_perror("fcntl O_NONBLOCK");
		close(sock);
		return -1;
	}
#endif
	return 0;
}

static inline int inprogress(void)
{
#ifdef _WIN32
	return WSAGetLastError() == WSAEWOULDBLOCK;
#else
	return errno == EINPROGRESS;
#endif
}

#ifdef IPV4
int connect_socket(struct connection *conn, char *hostname, char *port_in)
{
	struct sockaddr_in sock_name;
	int sock;
	struct hostent *host;
	int port = strtol(port_in, NULL, 10);

	host = gethostbyname(hostname);
	if (!host) {
		printf("Unable to get host %s\n", hostname);
		return -1;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		my_perror("socket");
		return -1;
	}

	if (set_non_blocking(sock))
		return -1;

	if (!set_conn_socket(conn, sock)) {
		printf("Problems! Could not set socket\n");
		close(sock);
		return -1;
	}

	memset(&sock_name, 0, sizeof(sock_name));
	sock_name.sin_family = AF_INET;
	sock_name.sin_addr.s_addr = *(unsigned *)host->h_addr_list[0];
	sock_name.sin_port = htons((short)port);

	if (connect(sock, (struct sockaddr *)&sock_name, sizeof(sock_name))) {
		if (inprogress()) {
			if (verbose > 1)
				printf("Connection deferred\n");
			return 0;
		} else {
			char errstr[100];

			sprintf(errstr, "connect %.80s", hostname);
			my_perror(errstr);
			close(sock);
			return -1;
		}
	} else
		return tcp_connected(conn);
}
#else
int connect_socket(struct connection *conn, char *hostname, char *port)
{
	int sock, deferred;
	struct addrinfo hints, *result, *r;

	/* We need this or we will get tcp and udp */
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(hostname, port, &hints, &result)) {
		printf("Unable to get host %s\n", hostname);
		return -1;
	}

	for (r = result; r; r = r->ai_next) {
		sock = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (sock < 0)
			continue;

		if (set_non_blocking(sock))
			continue;

		if (connect(sock, r->ai_addr, r->ai_addrlen)) {
			if (inprogress()) {
				if (verbose > 1)
					printf("Connection deferred\n");
				deferred = 1;
				break; /* got one */
			} else
				close(sock);
		} else {
			deferred = 0;
			break; /* got one */
		}
	}

	freeaddrinfo(result);

	if (!r) {
		printf("Unable to get socket for host %s\n", hostname);
		return -1;
	}

	if (!set_conn_socket(conn, sock)) {
		printf("Problems! Could not set socket\n");
		close(sock);
		return -1;
	}

	if (deferred)
		return 0;
	else
		return tcp_connected(conn);
}
#endif

/* This should only be called if conn->connected == 0 */
void check_connect(struct connection *conn)
{
	int so_error;
	socklen_t optlen = sizeof(so_error);

#ifdef WANT_SSL
	if (conn->ssl) {
		/* Wait for openssl connection to connect */
		if (openssl_check_connect(conn))
			fail_connection(conn);
		return;
	}
#endif

	if (getsockopt(conn->poll->fd, SOL_SOCKET, SO_ERROR,
		       (char *)&so_error, &optlen) == 0 &&
	    so_error == 0)
		tcp_connected(conn);
}



