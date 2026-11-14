/*
 * posix_common.c - helpers shared by the POSIX layer: errno, path names
 * and mounting on first use.
 */
#include <errno.h>
#include <string.h>

#include "posix_internal.h"

/* What the first file operation does if the volume is not mounted:
 *   0  nothing: the program calls pico_spiffs_mount() itself
 *   1  mount; flash with no volume gives ENODEV
 *   2  mount, formatting the flash first only if it is blank (default)
 *   3  mount, formatting anything that is not a matching volume
 */
#ifndef PICO_SPIFFS_AUTO_MOUNT
#define PICO_SPIFFS_AUTO_MOUNT 2
#endif

static pico_spiffs_mount_mode_t auto_mount_mode(void) {
    switch (PICO_SPIFFS_AUTO_MOUNT) {
    case 3:  return PICO_SPIFFS_FORMAT_IF_NEEDED;
    case 2:  return PICO_SPIFFS_FORMAT_IF_BLANK;
    default: return PICO_SPIFFS_MOUNT_ONLY;
    }
}

bool posix_volume_ready(void) {
    if (pico_spiffs_mounted())
        return true;
    if (PICO_SPIFFS_AUTO_MOUNT == 0) {
        errno = ENODEV;
        return false;
    }
    int res = pico_spiffs_mount(auto_mount_mode());
    if (res != SPIFFS_OK) {
        errno = pico_spiffs_errno(res);
        return false;
    }
    return true;
}

int posix_fail(int spiffs_error) {
    errno = pico_spiffs_errno(spiffs_error);
    return -1;
}

int posix_fail_errno(int error_number) {
    errno = error_number;
    return -1;
}

static const char *skip_leading_dirs(const char *path) {
    for (;;) {
        if (path[0] == '/')
            path++;
        else if (path[0] == '.' && path[1] == '/')
            path += 2;
        else
            return path;
    }
}

/* "/", ".", "./" and the like; not "", which names nothing (ENOENT). */
bool posix_is_root(const char *path) {
    if (path[0] == '\0')
        return false;
    const char *name = skip_leading_dirs(path);
    return name[0] == '\0' || strcmp(name, ".") == 0;
}

const char *posix_spiffs_name(const char *path) {
    if (path == NULL) {
        errno = EFAULT;
        return NULL;
    }
    const char *name = skip_leading_dirs(path);
    if (name[0] == '\0') {
        errno = ENOENT;
        return NULL;
    }
    if (strlen(name) > SPIFFS_OBJ_NAME_LEN - 1) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    return name;
}
