/*
 * posix_internal.h - shared by the POSIX layer (posix_*.c).
 *
 * File descriptors are SPIFFS file handles: SPIFFS numbers them from
 * PICO_SPIFFS_FIRST_FD (3), after stdin, stdout and stderr.
 */
#ifndef POSIX_INTERNAL_H
#define POSIX_INTERNAL_H

#include <stdbool.h>
#include <sys/stat.h>

#include "pico_spiffs.h"
#include "pico_spiffs_config.h"

#define STDIO_FD_COUNT 3

static inline bool is_stdio_fd(int fd) {
    return fd >= 0 && fd < STDIO_FD_COUNT;
}

/* Mount on first use if the volume is not mounted (see PICO_SPIFFS_AUTO_MOUNT).
 * Returns false, with errno set, if there is no volume to use. */
bool posix_volume_ready(void);

/* Set errno for a SPIFFS error code and return -1. */
int posix_fail(int spiffs_error);

/* Set errno and return -1. */
int posix_fail_errno(int error_number);

/* The SPIFFS name for a path: leading "/" and "./" removed, so "/a.txt",
 * "./a.txt" and "a.txt" are the same file. Returns NULL, with errno set to
 * ENAMETOOLONG or ENOENT, if no SPIFFS name can stand for the path. */
const char *posix_spiffs_name(const char *path);

/* True for the paths of the volume's only directory: "/", ".", "./". */
bool posix_is_root(const char *path);

/* Give a large file just opened an index map, if one is free
 * (posix_maps.c); release it again when the file is closed. */
void posix_map_if_large(int fd);
void posix_unmap(int fd);

/* Fill in a struct stat for a file. */
void posix_fill_stat(struct stat *st, const spiffs_stat *s);

#endif
