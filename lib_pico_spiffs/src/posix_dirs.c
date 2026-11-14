/*
 * posix_dirs.c - opendir(), readdir() and friends for the volume's one
 * directory, "/" (see include/sys/dirent.h).
 */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "posix_internal.h"

_Static_assert(PICO_SPIFFS_NAME_MAX == SPIFFS_OBJ_NAME_LEN - 1,
               "PICO_SPIFFS_NAME_MAX must be SPIFFS_OBJ_NAME_LEN - 1");

struct pico_spiffs_dir {
    spiffs_DIR spiffs_dir;
    struct dirent entry;
    long position;              /* entries returned so far, for telldir() */
};

static bool start_listing(DIR *dir) {
    dir->position = 0;
    return SPIFFS_opendir(pico_spiffs_fs(), "/", &dir->spiffs_dir) != NULL;
}

DIR *opendir(const char *path) {
    if (!posix_volume_ready())
        return NULL;
    if (path == NULL || !posix_is_root(path)) {
        const char *name = path ? posix_spiffs_name(path) : NULL;
        spiffs_stat s;
        errno = name && SPIFFS_stat(pico_spiffs_fs(), name, &s) == SPIFFS_OK ? ENOTDIR : ENOENT;
        return NULL;
    }
    DIR *dir = calloc(1, sizeof(DIR));
    if (dir == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    if (!start_listing(dir)) {
        errno = pico_spiffs_errno(SPIFFS_errno(pico_spiffs_fs()));
        free(dir);
        return NULL;
    }
    return dir;
}

/* The next file, or NULL at the end (errno unchanged) or on error. */
static struct dirent *next_entry(DIR *dir) {
    struct spiffs_dirent e;
    while (SPIFFS_readdir(&dir->spiffs_dir, &e) != NULL) {
        if (e.type != SPIFFS_TYPE_FILE)
            continue;
        dir->entry.d_ino = e.obj_id;
        dir->entry.d_type = DT_REG;
        memcpy(dir->entry.d_name, e.name, sizeof(dir->entry.d_name));
        dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
        dir->position++;
        return &dir->entry;
    }
    return NULL;
}

struct dirent *readdir(DIR *dir) {
    if (dir == NULL) {
        errno = EBADF;
        return NULL;
    }
    return next_entry(dir);
}

int readdir_r(DIR *dir, struct dirent *entry, struct dirent **result) {
    if (dir == NULL)
        return EBADF;
    struct dirent *next = next_entry(dir);
    if (next != NULL)
        *entry = *next;
    *result = next != NULL ? entry : NULL;
    return 0;
}

void rewinddir(DIR *dir) {
    if (dir != NULL)
        start_listing(dir);
}

long telldir(DIR *dir) {
    return dir != NULL ? dir->position : -1;
}

void seekdir(DIR *dir, long position) {
    if (dir == NULL)
        return;
    start_listing(dir);
    while (dir->position < position && next_entry(dir) != NULL) {
    }
}

int closedir(DIR *dir) {
    if (dir == NULL)
        return posix_fail_errno(EBADF);
    SPIFFS_closedir(&dir->spiffs_dir);
    free(dir);
    return 0;
}
