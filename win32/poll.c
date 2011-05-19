#include <winsock2.h>
#include <windows.h>

int poll(struct pollfd *fds, int nfds, int timeout)
{
	fd_set readfds;
	fd_set writefds;
	fd_set errorfds;
	int i, n;
	unsigned max = 0;

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&errorfds);
	for (i = 0; i < nfds; ++i) {
		fds[i].revents = 0;
		if (fds[i].fd != -1) {
			if (fds[i].events & POLLIN)
				FD_SET(fds[i].fd, &readfds);
			if (fds[i].events & POLLOUT)
				FD_SET(fds[i].fd, &writefds);
			FD_SET(fds[i].fd, &errorfds);
			if (fds[i].fd > max)
				max = (unsigned)fds[i].fd;
		}
	}

	if (timeout < 0)
		n = select(max + 1, &readfds, &writefds, &errorfds, NULL);
	else {
		struct timeval tv;

		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;

		n = select(max + 1, &readfds, &writefds, &errorfds, &tv);
	}

	if (n > 0)
		for (i = 0; i < nfds; ++i)
			if (fds[i].fd != -1) {
				if (FD_ISSET(fds[i].fd, &readfds))
					fds[i].revents |= POLLIN;
				if (FD_ISSET(fds[i].fd, &writefds))
					fds[i].revents |= POLLOUT;
				if (FD_ISSET(fds[i].fd, &errorfds))
					fds[i].revents |= POLLERR;
			}

	return n;
}
