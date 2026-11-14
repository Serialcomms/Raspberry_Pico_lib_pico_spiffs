/*
 * sys/dirent.h - directory entries for pico_spiffs.
 *
 * newlib's own <sys/dirent.h> stops the build ("<dirent.h> not supported").
 * pico_spiffs puts this one first on the include path, so the standard
 * <dirent.h> works: opendir(), readdir(), readdir_r(), rewinddir(),
 * telldir(), seekdir() and closedir(). The volume has one directory, its
 * root, opened as "/" or ".".
 */
#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest file name, without its NUL (SPIFFS_OBJ_NAME_LEN - 1). */
#ifndef PICO_SPIFFS_NAME_MAX
#define PICO_SPIFFS_NAME_MAX 31
#endif

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
    ino_t d_ino;                            /* SPIFFS object id */
    unsigned char d_type;                   /* DT_REG */
    char d_name[PICO_SPIFFS_NAME_MAX + 1];
};

typedef struct pico_spiffs_dir DIR;

#ifdef __cplusplus
}
#endif

#endif /* _SYS_DIRENT_H */
