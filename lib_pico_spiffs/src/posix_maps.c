/*
 * posix_maps.c - index maps for large files opened through the POSIX layer.
 *
 * A SPIFFS index map keeps a file's data page locations in RAM, so reading
 * anywhere in the file goes straight to the right page. When a file of at
 * least PICO_SPIFFS_INDEX_MAP_MIN_FILE bytes is opened, it gets one of
 * PICO_SPIFFS_INDEX_MAPS maps if one is free, covering the start of the
 * file (PICO_SPIFFS_INDEX_MAP_BYTES of RAM: 2 bytes per logical page).
 * Writes keep the map up to date; reads beyond it work as usual.
 */
#include "posix_internal.h"

#ifndef PICO_SPIFFS_INDEX_MAPS
#define PICO_SPIFFS_INDEX_MAPS 2
#endif
#ifndef PICO_SPIFFS_INDEX_MAP_BYTES
#define PICO_SPIFFS_INDEX_MAP_BYTES 8192
#endif
#ifndef PICO_SPIFFS_INDEX_MAP_MIN_FILE
#define PICO_SPIFFS_INDEX_MAP_MIN_FILE (64u * 1024u)
#endif

#if PICO_SPIFFS_INDEX_MAPS > 0

#define MAP_ENTRIES (PICO_SPIFFS_INDEX_MAP_BYTES / sizeof(spiffs_page_ix))

typedef struct {
    int fd;                     /* 0: free */
    spiffs_ix_map map;
    spiffs_page_ix entries[MAP_ENTRIES];
} index_map_t;

static index_map_t maps[PICO_SPIFFS_INDEX_MAPS];

/* A map whose file was removed while open has lost its descriptor. */
static bool fd_still_open(spiffs *fs, int fd) {
    spiffs_stat s;
    return SPIFFS_fstat(fs, (spiffs_file)fd, &s) == SPIFFS_OK;
}

static void release(int fd) {
    for (int i = 0; i < PICO_SPIFFS_INDEX_MAPS; i++)
        if (maps[i].fd == fd)
            maps[i].fd = 0;
}

static index_map_t *free_map(spiffs *fs) {
    for (int i = 0; i < PICO_SPIFFS_INDEX_MAPS; i++) {
        if (maps[i].fd != 0 && !fd_still_open(fs, maps[i].fd))
            maps[i].fd = 0;
        if (maps[i].fd == 0)
            return &maps[i];
    }
    return NULL;
}

static bool large(spiffs *fs, int fd) {
    spiffs_stat s;
    return SPIFFS_fstat(fs, (spiffs_file)fd, &s) == SPIFFS_OK &&
           s.size >= PICO_SPIFFS_INDEX_MAP_MIN_FILE;
}

void posix_map_if_large(int fd) {
    spiffs *fs = pico_spiffs_fs();
    pico_spiffs_lock(fs);
    release(fd);                /* a new file on a reused descriptor */
    index_map_t *slot = large(fs, fd) ? free_map(fs) : NULL;
    if (slot != NULL) {
        /* one entry spare: the API's sizing allows for an unaligned start */
        u32_t bytes = (u32_t)SPIFFS_ix_map_entries_to_bytes(fs, MAP_ENTRIES - 1);
        if (SPIFFS_ix_map(fs, (spiffs_file)fd, &slot->map, 0, bytes, slot->entries) == SPIFFS_OK)
            slot->fd = fd;
    }
    pico_spiffs_unlock(fs);
}

void posix_unmap(int fd) {
    spiffs *fs = pico_spiffs_fs();
    pico_spiffs_lock(fs);
    release(fd);                /* closing the file unmaps it in SPIFFS */
    pico_spiffs_unlock(fs);
}

#else

void posix_map_if_large(int fd) {
    (void)fd;
}

void posix_unmap(int fd) {
    (void)fd;
}

#endif
