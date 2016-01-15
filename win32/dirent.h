#ifndef _DIRENT_H_
#define _DIRENT_H_

#define DT_REG 0
#define DT_DIR 1

struct dirent {
	unsigned char d_type;
	char *d_name;
};

typedef struct {
	HANDLE hfind;
	WIN32_FIND_DATA ffd;
	struct dirent ent;
	int first;
} DIR;

DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#endif
