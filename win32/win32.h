/* WIN32 specific include file */

/* winsock2.h must be before windows.h to avoid winsock.h clashes */
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <direct.h> /* for chdir */

/* We only use read/write/close on sockets */
/* We use stream operations on files */
#define close closesocket
#define read(s, b, n)  recv(s, b, n, 0)
#define write(s, b, n) send(s, b, n, 0)

#define socklen_t int

#define unlink _unlink
#define strdup _strdup
#define chdir _chdir
#define stricmp _stricmp
#define inline _inline

/* from win32.c */
void win32_init(void);
