#include "get-comics.h"
#include <fcntl.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif


static int tcp_connected(struct connection *conn)
{
	set_writable(conn);

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

int connect_socket(struct connection *conn, char *hostname, int port)
{
	struct sockaddr_in sock_name;
	int sock;
#ifdef _WIN32
	u_long optval;
#else
	int optval;
#endif
	struct hostent *host;

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

#ifdef _WIN32
	optval = 1;
	if (ioctlsocket(sock, FIONBIO, &optval)) {
		printf("ioctlsocket FIONBIO failed\n");
		close(sock);
		return -1;
	}
#else
	optval = fcntl(sock, F_GETFL, 0);
	if (optval == -1 || fcntl(sock, F_SETFL, optval | O_NONBLOCK)) {
		my_perror("fcntl O_NONBLOCK");
		close(sock);
		return -1;
	}
#endif

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
			set_writable(conn);
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



