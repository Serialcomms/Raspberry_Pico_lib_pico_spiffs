/*
 * posix_names.c - system calls that work on names: stat, unlink, rename,
 * link, mkdir and rmdir.
 *
 * The volume is flat: its only directory is the root, "/". Names may
 * contain '/', but there are no other directories to make or remove.
 *
 * rename() is provided here rather than as newlib's _link + _unlink pair,
 * as SPIFFS renames in place; like POSIX rename() it replaces an existing
 * target.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "posix_internal.h"

int _stat(const char *path, struct stat *st) {
    if (!posix_volume_ready())
        return -1;
    if (path != NULL && posix_is_root(path)) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
        st->st_nlink = 2;
        return 0;
    }
    const char *name = posix_spiffs_name(path);
    if (name == NULL)
        return -1;
    spiffs_stat s;
    s32_t res = SPIFFS_stat(pico_spiffs_fs(), name, &s);
    if (res < 0)
        return posix_fail(res);
    posix_fill_stat(st, &s);
    return 0;
}

int _unlink(const char *path) {
    if (!posix_volume_ready())
        return -1;
    if (path != NULL && posix_is_root(path))
        return posix_fail_errno(EPERM);
    const char *name = posix_spiffs_name(path);
    if (name == NULL)
        return -1;
    s32_t res = SPIFFS_remove(pico_spiffs_fs(), name);
    return res < 0 ? posix_fail(res) : 0;
}

/* Hard links need a link count; SPIFFS files have exactly one name. */
int _link(const char *existing, const char *new_path) {
    (void)existing;
    (void)new_path;
    return posix_fail_errno(EMLINK);
}

static bool file_exists(spiffs *fs, const char *name) {
    spiffs_stat s;
    return SPIFFS_stat(fs, name, &s) == SPIFFS_OK;
}

/* Rename, replacing an existing target (lock held). */
static s32_t rename_replacing(spiffs *fs, const char *from, const char *to) {
    if (strcmp(from, to) == 0)
        return file_exists(fs, from) ? SPIFFS_OK : SPIFFS_ERR_NOT_FOUND;
    s32_t res = SPIFFS_rename(fs, from, to);
    if (res != SPIFFS_ERR_CONFLICTING_NAME)
        return res;
    if (!file_exists(fs, from))
        return SPIFFS_ERR_NOT_FOUND;
    res = SPIFFS_remove(fs, to);
    return res < 0 ? res : SPIFFS_rename(fs, from, to);
}

int rename(const char *old_path, const char *new_path) {
    if (!posix_volume_ready())
        return -1;
    if (old_path == NULL || new_path == NULL)
        return posix_fail_errno(EFAULT);
    if (posix_is_root(old_path) || posix_is_root(new_path))
        return posix_fail_errno(EBUSY);
    const char *from = posix_spiffs_name(old_path);
    const char *to = posix_spiffs_name(new_path);
    if (from == NULL || to == NULL)
        return -1;
    spiffs *fs = pico_spiffs_fs();
    pico_spiffs_lock(fs);
    s32_t res = rename_replacing(fs, from, to);
    pico_spiffs_unlock(fs);
    return res < 0 ? posix_fail(res) : 0;
}

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    if (!posix_volume_ready())
        return -1;
    if (posix_is_root(path))
        return posix_fail_errno(EEXIST);
    return posix_fail_errno(ENOSYS);        /* no subdirectories */
}

int rmdir(const char *path) {
    if (!posix_volume_ready())
        return -1;
    if (posix_is_root(path))
        return posix_fail_errno(EBUSY);
    const char *name = posix_spiffs_name(path);
    if (name == NULL)
        return -1;
    return posix_fail_errno(file_exists(pico_spiffs_fs(), name) ? ENOTDIR : ENOENT);
}
