/* WIN32 specific include file */

/* winsock2.h must be before windows.h to avoid winsock.h clashes */
#include <winsock2.h>
/* Only needed for getaddrinfo. If you set IPV4 in socket.c, you can
 * remove this include. */
#include <w2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h> /* for chdir */

#define socklen_t int

#define open _open
#define unlink _unlink
#define strdup _strdup
#define chdir _chdir
#define stricmp _stricmp
#define inline _inline
#define strcasecmp stricmp
#define snprintf _snprintf

#define S_ISREG(m) ((m) & _S_IFREG)

/* from win32.c */
void win32_init(void);

/* from poll.c */
int poll(struct pollfd *fds, int nfds, int timeout);
