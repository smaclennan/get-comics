#include <windows.h>
#include <stdio.h> /* snprintf */
#include <dirent.h>

DIR *opendir(const char *name)
{
	DIR *dir = calloc(1, sizeof(DIR));
	if (dir) {
		char path[MAX_PATH];

		_snprintf(path, sizeof(path), "%s/*", name);
		dir->first = 1;
		dir->ent.d_name = dir->ffd.cFileName;
		dir->hfind = FindFirstFile(path, &dir->ffd);
		if (dir->hfind == INVALID_HANDLE_VALUE) {
			free(dir);
			dir = NULL;
		}
	}
	return dir;
}

struct dirent *readdir(DIR *dirp)
{
	if (dirp->hfind == INVALID_HANDLE_VALUE)
		return NULL;
	else if (dirp->first)
		dirp->first = 0;
	else if (FindNextFile(dirp->hfind, &dirp->ffd) == 0)
		return NULL;

	if (dirp->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		dirp->ent.d_type = DT_DIR;
	else
		dirp->ent.d_type = DT_REG;

	return &dirp->ent;
}

int closedir(DIR *dirp)
{
	FindClose(dirp->hfind);
	free(dirp);
	return 0;
}
