/* dirent.h - host only: the library's DIR type, with its functions renamed
 * (pico_opendir, ...) so they do not clash with the host C library. */
#ifndef HOST_DIRENT_H
#define HOST_DIRENT_H
#include "sys/dirent.h"
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int readdir_r(DIR *, struct dirent *, struct dirent **);
void rewinddir(DIR *);
long telldir(DIR *);
void seekdir(DIR *, long);
int closedir(DIR *);
#endif
