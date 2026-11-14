/*
 * fuzz.c - model-based fuzzer for the SPIFFS core.
 *
 * Usage: fuzz [first seed] [seed count] [operations per seed]
 *
 * Each seed picks a volume geometry and a cache size, then performs random
 * operations (open with random flags, write, read, seek, close, remove,
 * rename, stat, list, remount, check, gc) on SPIFFS and, in parallel, on a
 * simple model of what the files should contain. Every result and every
 * byte read is compared with the model; the first difference stops the run
 * and prints the seed and the recent operations, so it can be replayed.
 *
 * The flash is the simulated NOR chip in tests/common, which also reports
 * any attempt to program a 0 bit back to 1. The SPIFFS lock is checked to
 * be released after every call.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spiffs.h"
#include "spiffs_nucleus.h"     /* sizeof(spiffs_fd) */
#include "nor_flash.h"

#define MAX_FILES       8
#define MAX_FDS         5           /* the model's handles; SPIFFS has one more */
#define MAX_FILE_BYTES  (96 * 1024)
#define MAX_IO          3000
#ifndef HISTORY
#define HISTORY         40
#endif

/* ------------------------------------------------------------------ */
/* Random numbers                                                      */
/* ------------------------------------------------------------------ */

static uint64_t rng_state;

static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

static uint32_t below(uint32_t n) {
    return n == 0 ? 0 : rnd() % n;
}

static bool chance(uint32_t percent) {
    return below(100) < percent;
}

/* ------------------------------------------------------------------ */
/* Operation history, printed on failure                               */
/* ------------------------------------------------------------------ */

static char history[HISTORY][120];
static uint32_t history_next;
static uint32_t current_seed, current_op;

static void note(const char *format, ...) {
    char *line = history[history_next % HISTORY];
    int used = snprintf(line, sizeof(history[0]), "%5u ", current_op);
    va_list args;
    va_start(args, format);
    vsnprintf(line + used, sizeof(history[0]) - (size_t)used, format, args);
    va_end(args);
    history_next++;
}

static void fail(const char *format, ...) {
    printf("\nFAIL seed %u op %u: ", current_seed, current_op);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\nrecent operations:\n");
    uint32_t start = history_next > HISTORY ? history_next - HISTORY : 0;
    for (uint32_t i = start; i < history_next; i++)
        printf("  %s\n", history[i % HISTORY]);
    exit(1);
}

/* ------------------------------------------------------------------ */
/* Flash and lock hooks                                                */
/* ------------------------------------------------------------------ */

static nor_flash_t flash;
static int lock_depth;

void pico_spiffs_lock(struct spiffs_t *fs) {
    (void)fs;
    lock_depth++;
}

void pico_spiffs_unlock(struct spiffs_t *fs) {
    (void)fs;
    if (--lock_depth < 0)
        fail("SPIFFS unlocked more often than locked");
}

static s32_t hal_read(u32_t addr, u32_t size, u8_t *dst) {
    return nor_flash_read(&flash, addr, size, dst) == 0 ? SPIFFS_OK : SPIFFS_ERR_INTERNAL;
}

static s32_t hal_write(u32_t addr, u32_t size, u8_t *src) {
    return nor_flash_program(&flash, addr, size, src) == 0 ? SPIFFS_OK : SPIFFS_ERR_INTERNAL;
}

static s32_t hal_erase(u32_t addr, u32_t size) {
    return nor_flash_erase(&flash, addr, size) == 0 ? SPIFFS_OK : SPIFFS_ERR_INTERNAL;
}

/* ------------------------------------------------------------------ */
/* The volume                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t page, block, erase, size;
} geometry_t;

static const geometry_t geometries[] = {
    {  256,  4096, 4096,  64 * 1024 },
    {  256,  4096, 4096, 256 * 1024 },
    {  256,  8192, 4096, 128 * 1024 },
    {  512,  8192, 4096, 256 * 1024 },
    { 1024, 16384, 4096, 512 * 1024 },
    {  256, 65536, 4096, 512 * 1024 },
    {  512, 65536, 65536, 1024 * 1024 },
};

static spiffs fs;
static spiffs_config cfg;
static geometry_t geo;
static uint32_t cache_pages;
static uint32_t work[2 * 1024 / 4];                 /* two pages, 4-byte aligned */
static uint32_t fd_space[((MAX_FDS + 1) * sizeof(spiffs_fd) + 3) / 4];
static uint32_t cache[(1024 + 64) * 8 / 4];

static uint32_t fd_space_bytes(void) {
    return (uint32_t)((MAX_FDS + 1) * sizeof(spiffs_fd));
}

static s32_t mount(void) {
    cfg.phys_size = geo.size;
    cfg.phys_addr = 0;
    cfg.phys_erase_block = geo.erase;
    cfg.log_block_size = geo.block;
    cfg.log_page_size = geo.page;
    cfg.hal_read_f = hal_read;
    cfg.hal_write_f = hal_write;
    cfg.hal_erase_f = hal_erase;
    cfg.fh_ix_offset = 2;
    uint32_t cache_bytes = (uint32_t)(sizeof(spiffs_cache) +
                                      cache_pages * (sizeof(spiffs_cache_page) + geo.page));
    return SPIFFS_mount(&fs, &cfg, (u8_t *)work, (u8_t *)fd_space, fd_space_bytes(),
                        cache, cache_bytes, NULL);
}

static void check_lock(const char *what) {
    if (lock_depth != 0)
        fail("lock still held (%d) after %s", lock_depth, what);
}

/* ------------------------------------------------------------------ */
/* The model                                                           */
/* ------------------------------------------------------------------ */

/* Writes made since a file was last known to be on flash. SPIFFS may
 * store any of them before a later flush fails, so after a lost cache a
 * byte may hold the value of any one of them. */
#define MAX_PENDING 64
typedef struct {
    uint32_t offset, len;
    uint8_t *data;
} pending_write_t;

typedef struct {
    pending_write_t writes[MAX_PENDING];
    int count;
    bool overflowed;            /* too many to track: accept any value */
} pending_t;

static void pending_clear(pending_t *p) {
    for (int i = 0; i < p->count; i++)
        free(p->writes[i].data);
    p->count = 0;
    p->overflowed = false;
}

static void pending_add(pending_t *p, uint32_t offset, const uint8_t *data, uint32_t len) {
    if (p->count == MAX_PENDING) {
        p->overflowed = true;
        return;
    }
    pending_write_t *w = &p->writes[p->count++];
    *w = (pending_write_t){ offset, len, malloc(len + 1) };
    memcpy(w->data, data, len);
}

static void pending_copy(pending_t *to, const pending_t *from) {
    to->count = 0;
    to->overflowed = from->overflowed;
    for (int i = 0; i < from->count; i++)
        pending_add(to, from->writes[i].offset, from->writes[i].data, from->writes[i].len);
}

static bool pending_has(const pending_t *p, uint32_t b, uint8_t value) {
    if (p->overflowed)
        return true;
    for (int i = 0; i < p->count; i++)
        if (b >= p->writes[i].offset && b < p->writes[i].offset + p->writes[i].len &&
            p->writes[i].data[b - p->writes[i].offset] == value)
            return true;
    return false;
}

typedef struct {
    bool exists;
    char name[SPIFFS_OBJ_NAME_LEN + 8];
    uint8_t *data;
    uint32_t size;
    uint8_t *durable;           /* contents when last known to be on flash */
    uint32_t durable_size;
    pending_t pending;          /* writes since then */
} model_file_t;

typedef struct {
    bool open;
    spiffs_file fh;
    int file;                   /* index in files[] */
    spiffs_flags flags;
    uint32_t offset;
    bool mapped;
    spiffs_ix_map map;
    spiffs_page_ix map_buf[256];
} model_fd_t;

static model_file_t files[MAX_FILES];
static model_fd_t fds[MAX_FDS];
static uint8_t io_buffer[MAX_IO + 16];
static uint8_t read_buffer[MAX_FILE_BYTES + 16];

static const char *const fixed_names[] = { "a", "disk.img", "/rk05.dsk", "x y",
                                           "UPPER.TXT", "" };

/* A name for file slot i: some fixed, one exactly the longest allowed. */
static void make_name(int i, char *name) {
    if (i < (int)(sizeof(fixed_names) / sizeof(fixed_names[0])))
        strcpy(name, fixed_names[i]);
    else if (i == MAX_FILES - 1)
        memset(name, 'L', SPIFFS_OBJ_NAME_LEN - 1), name[SPIFFS_OBJ_NAME_LEN - 1] = 0;
    else
        sprintf(name, "file%d", i);
}

static void model_reset(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        free(files[i].data);
        free(files[i].durable);
        pending_clear(&files[i].pending);
        memset(&files[i], 0, sizeof(files[i]));
        files[i].data = calloc(1, MAX_FILE_BYTES);
        files[i].durable = calloc(1, MAX_FILE_BYTES);
        make_name(i, files[i].name);
    }
    memset(fds, 0, sizeof(fds));
}

static int file_by_name(const char *name) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].exists && strcmp(files[i].name, name) == 0)
            return i;
    return -1;
}

static int open_fd_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_FDS; i++)
        n += fds[i].open;
    return n;
}

static void close_model_fds_of(int file) {
    for (int i = 0; i < MAX_FDS; i++)
        if (fds[i].open && fds[i].file == file)
            fds[i].open = fds[i].mapped = false;
}

/* The model's contents are now on flash (a flush succeeded). */
static void mark_durable(int i) {
    memcpy(files[i].durable, files[i].data, files[i].size);
    files[i].durable_size = files[i].size;
    pending_clear(&files[i].pending);
}

static void mark_all_durable(void) {
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].exists)
            mark_durable(i);
}

/* A file's durable contents, saved before reading it back: reading
 * settles the caches, which updates files[].durable (and may adopt other
 * files, so each check needs its own copy). */
typedef struct {
    uint8_t *bytes;
    uint32_t size;
    pending_t pending;
} snapshot_t;

static snapshot_t save_durable(int i) {
    snapshot_t snap = { malloc(MAX_FILE_BYTES), files[i].durable_size, { .count = 0 } };
    memcpy(snap.bytes, files[i].durable, snap.size);
    pending_copy(&snap.pending, &files[i].pending);
    return snap;
}

static void free_snapshot(snapshot_t *snap) {
    free(snap->bytes);
    pending_clear(&snap->pending);
}

/* After cached data was lost, a byte may hold the model's value, the last
 * flushed value, or zero where a gap was filled. */
static bool byte_plausible(int i, const snapshot_t *old, uint32_t b, uint8_t value) {
    if (b < files[i].size && value == files[i].data[b]) return true;
    if (b < old->size && value == old->bytes[b]) return true;
    if (pending_has(&old->pending, b, value)) return true;
    return value == 0;
}

/* ------------------------------------------------------------------ */
/* Comparing SPIFFS with the model                                     */
/* ------------------------------------------------------------------ */

static void expect_result(const char *what, s32_t got, s32_t want) {
    if (got != want)
        fail("%s returned %" PRId32 ", expected %" PRId32 " (errno %" PRId32 ")",
             what, got, want, SPIFFS_errno(&fs));
}

static void settle_caches(void);

/* Read a whole file through a fresh handle into read_buffer. */
static s32_t read_whole_file(const char *name, uint32_t *size) {
    settle_caches();
    spiffs_file fh = SPIFFS_open(&fs, name, SPIFFS_O_RDONLY, 0);
    check_lock("open for verify");
    if (fh < 0)
        return fh;
    uint32_t total = 0;
    for (;;) {
        s32_t n = SPIFFS_read(&fs, fh, read_buffer + total, MAX_IO);
        check_lock("read for verify");
        if (n < 0) {
            SPIFFS_close(&fs, fh);
            return n;
        }
        total += (uint32_t)n;
        if (total > MAX_FILE_BYTES)
            fail("file %s longer than anything written", name);
        if (n == 0)
            break;
    }
    SPIFFS_close(&fs, fh);
    check_lock("close for verify");
    *size = total;
    return SPIFFS_OK;
}

static void verify_file(int i) {
    uint32_t size = 0;
    s32_t res = read_whole_file(files[i].name, &size);
    if (res != SPIFFS_OK)
        fail("verify '%s': read failed %" PRId32, files[i].name, res);
    if (size != files[i].size)
        fail("verify '%s': size %u, model %u", files[i].name, size, files[i].size);
    for (uint32_t b = 0; b < size; b++)
        if (read_buffer[b] != files[i].data[b])
            fail("verify '%s': byte %u is %02x, model %02x", files[i].name, b,
                 read_buffer[b], files[i].data[b]);
}

static void verify_directory(void) {
    bool seen[MAX_FILES] = { false };
    settle_caches();
    spiffs_DIR dir;
    struct spiffs_dirent entry;
    if (SPIFFS_opendir(&fs, "/", &dir) == NULL)
        fail("opendir failed");
    while (SPIFFS_readdir(&dir, &entry) != NULL) {
        int i = file_by_name((const char *)entry.name);
        if (i < 0)
            fail("directory lists unknown file '%s'", entry.name);
        if (seen[i])
            fail("directory lists '%s' twice", entry.name);
        if (entry.size != files[i].size)
            fail("directory size of '%s' %u, model %u", entry.name, entry.size, files[i].size);
        seen[i] = true;
    }
    SPIFFS_closedir(&dir);
    check_lock("readdir");
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].exists && !seen[i])
            fail("directory misses '%s'", files[i].name);
}

static void verify_all(void) {
    if (open_fd_count() >= MAX_FDS + 1)
        return;
    for (int i = 0; i < MAX_FILES; i++)
        if (files[i].exists)
            verify_file(i);
    verify_directory();
}

/* After SPIFFS_ERR_FULL a write or create may have partly happened: the
 * written range holds some prefix of the new data (pages are written in
 * order) and then older data; earlier cached writes may be lost too. Check
 * every byte is plausible and adopt what SPIFFS holds. */
static void resync_after_full(int i, uint32_t offset, const uint8_t *data, uint32_t len) {
    uint32_t size = 0;
    snapshot_t old = save_durable(i);
    s32_t res = read_whole_file(files[i].name, &size);
    if (res == SPIFFS_ERR_NOT_FOUND) {
        files[i].exists = false;
        close_model_fds_of(i);
        free_snapshot(&old);
        return;
    }
    if (res != SPIFFS_OK)
        fail("resync '%s': read failed %" PRId32, files[i].name, res);
    uint32_t longest = files[i].size > old.size ? files[i].size : old.size;
    if (size > longest && size > offset + len)
        fail("resync '%s': size %u beyond anything written", files[i].name, size);
    bool still_new = true;                  /* inside the new-data prefix */
    for (uint32_t b = 0; b < size; b++) {
        bool in_range = b >= offset && b < offset + len;
        if (in_range && still_new && read_buffer[b] == data[b - offset])
            continue;
        if (in_range)
            still_new = false;
        if (!byte_plausible(i, &old, b, read_buffer[b]))
            fail("resync '%s': byte %u is %02x: not new, model %02x or flushed %02x",
                 files[i].name, b, read_buffer[b], b < files[i].size ? files[i].data[b] : 0,
                 b < old.size ? old.bytes[b] : 0);
    }
    free_snapshot(&old);
    memcpy(files[i].data, read_buffer, size);
    files[i].size = size;
    mark_durable(i);
}

/* An operation that flushes the write cache (read, seek, fstat, flush,
 * close) can find the volume full; the cached data is then lost and the
 * error reported. Adopt what SPIFFS holds for that file. */
static void adopt_with_snapshot(model_fd_t *fd, snapshot_t old) {
    model_file_t *file = &files[fd->file];
    uint32_t size = 0;
    note("  volume full: adopting '%s' from flash", file->name);
    if (read_whole_file(file->name, &size) != SPIFFS_OK)
        fail("adopt '%s': read failed", file->name);
    int i = fd->file;
    for (uint32_t b = 0; b < size; b++)
        if (!byte_plausible(i, &old, b, read_buffer[b]))
            fail("adopt '%s': byte %u is %02x: not model %02x or flushed %02x", file->name, b,
                 read_buffer[b], b < file->size ? file->data[b] : 0,
                 b < old.size ? old.bytes[b] : 0);
    free_snapshot(&old);
    memcpy(file->data, read_buffer, size);
    file->size = size;
    mark_durable(i);
    s32_t tell = SPIFFS_tell(&fs, fd->fh);
    check_lock("tell");
    if (tell < 0)
        fail("tell after adopting '%s' returned %d", file->name, tell);
    fd->offset = (uint32_t)tell;    /* may be beyond the end: writes there zero-fill */
}

static void adopt_after_lost_cache(model_fd_t *fd) {
    adopt_with_snapshot(fd, save_durable(fd->file));
}

/* Flush each open handle's cached writes; a handle whose flush finds the
 * volume full has lost them, so adopt its file. Afterwards no cache holds
 * unwritten data, and opening a file cannot fail on someone else's flush. */
static void settle_caches(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (!fds[i].open)
            continue;
        s32_t res = SPIFFS_fflush(&fs, fds[i].fh);
        check_lock("fflush (settle)");
        if (res == SPIFFS_ERR_FULL && files[fds[i].file].exists)
            adopt_after_lost_cache(&fds[i]);
        else if (res != SPIFFS_OK && res != SPIFFS_ERR_FULL)
            fail("settle: fflush fh %d returned %" PRId32, fds[i].fh, res);
    }
    mark_all_durable();
}

/* open, stat, readdir, check, gc and info store every handle's cached
 * writes first; if the volume is full that fails, the cached data is lost
 * and the error is reported by that call. Adopt every open file. */
static void adopt_all_open_files(void) {
    snapshot_t old[MAX_FDS];
    for (int i = 0; i < MAX_FDS; i++)       /* before any read-back settles caches */
        if (fds[i].open && files[fds[i].file].exists)
            old[i] = save_durable(fds[i].file);
    for (int i = 0; i < MAX_FDS; i++)
        if (fds[i].open && files[fds[i].file].exists)
            adopt_with_snapshot(&fds[i], old[i]);
}

/* True (after adopting) if res is a full volume found by a cache flush. */
static bool lost_cache(model_fd_t *fd, s32_t res) {
    if (res != SPIFFS_ERR_FULL)
        return false;
    adopt_after_lost_cache(fd);
    return true;
}

/* ------------------------------------------------------------------ */
/* Operations                                                          */
/* ------------------------------------------------------------------ */

/* A truncation that ran out of space stops at a consistent shorter size:
 * the file must be a prefix of what it was. */
static void adopt_partial_truncation(int i) {
    uint32_t size = 0;
    note("  volume full during truncation: adopting '%s'", files[i].name);
    if (read_whole_file(files[i].name, &size) != SPIFFS_OK)
        fail("partial truncation of '%s': read failed", files[i].name);
    if (size > files[i].size || memcmp(read_buffer, files[i].data, size) != 0)
        fail("partial truncation of '%s' is not a prefix (size %u of %u)", files[i].name, size,
             files[i].size);
    files[i].size = size;
    mark_durable(i);
}

static int free_model_fd(void) {
    for (int i = 0; i < MAX_FDS; i++)
        if (!fds[i].open)
            return i;
    return -1;
}

static spiffs_flags random_open_flags(void) {
    static const spiffs_flags access[] = { SPIFFS_O_RDONLY, SPIFFS_O_WRONLY, SPIFFS_O_RDWR };
    spiffs_flags flags = access[below(3)];
    if (chance(50)) flags |= SPIFFS_O_CREAT;
    if (chance(20)) flags |= SPIFFS_O_TRUNC;
    if (chance(20)) flags |= SPIFFS_O_APPEND;
    if (chance(15)) flags |= SPIFFS_O_EXCL;
    return flags;
}

static s32_t expected_open(int file, spiffs_flags flags) {
    if (file < 0)
        return (flags & SPIFFS_O_CREAT) ? SPIFFS_OK : SPIFFS_ERR_NOT_FOUND;
    if ((flags & (SPIFFS_O_CREAT | SPIFFS_O_EXCL)) == (SPIFFS_O_CREAT | SPIFFS_O_EXCL))
        return SPIFFS_ERR_FILE_EXISTS;
    return SPIFFS_OK;
}

static void op_open(void) {
    int slot = (int)below(MAX_FILES);
    const char *name = files[slot].name;
    spiffs_flags flags = random_open_flags();
    int m = free_model_fd();
    if (m < 0)
        return;
    int file = file_by_name(name);
    note("open '%s' flags %02x", name, flags);
    spiffs_file fh = SPIFFS_open(&fs, name, flags, 0);
    check_lock("open");
    if (fh < 0)
        note("  -> %d", fh);
    s32_t want = expected_open(file, flags);
    if (fh != SPIFFS_ERR_FULL && fh != SPIFFS_ERR_NAME_TOO_LONG)
        mark_all_durable();
    if (fh == SPIFFS_ERR_FULL) {
        adopt_all_open_files();
        if (file < 0) {
            files[slot].exists = false;
            resync_after_full(slot, 0, NULL, 0);
        } else if (flags & SPIFFS_O_TRUNC) {
            adopt_partial_truncation(file);
        }
        return;
    }
    if (want != SPIFFS_OK) {
        expect_result("open", fh, want);
        return;
    }
    if (fh < 0)
        fail("open '%s' flags %02x failed %" PRId32, name, flags, fh);
    if (file < 0) {
        file = slot;
        files[slot].exists = true;
        files[slot].size = 0;
    } else if (flags & SPIFFS_O_TRUNC) {
        files[file].size = 0;
    }
    mark_durable(file);
    fds[m].open = true;
    fds[m].fh = fh;
    fds[m].file = file;
    fds[m].flags = flags;
    fds[m].offset = 0;
    fds[m].mapped = false;
    note("  -> fh %d", fh);
}

static model_fd_t *random_open_fd(void) {
    int start = (int)below(MAX_FDS);
    for (int i = 0; i < MAX_FDS; i++) {
        model_fd_t *fd = &fds[(start + i) % MAX_FDS];
        if (fd->open)
            return fd;
    }
    return NULL;
}

static uint32_t random_io_length(void) {
    if (chance(5)) return 0;
    if (chance(60)) return 1 + below(300);
    return 1 + below(MAX_IO);
}

static void fill_random(uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        data[i] = (uint8_t)rnd();
}

static void op_write(void) {
    model_fd_t *fd = random_open_fd();
    if (fd == NULL)
        return;
    model_file_t *file = &files[fd->file];
    uint32_t len = random_io_length();
    uint32_t offset = (fd->flags & SPIFFS_O_APPEND) ? file->size : fd->offset;
    if (offset + len > MAX_FILE_BYTES)
        return;
    fill_random(io_buffer, len);
    note("write fh %d len %u at %u (size %u)", fd->fh, len, offset, file->size);
    s32_t res = SPIFFS_write(&fs, fd->fh, io_buffer, (s32_t)len);
    check_lock("write");
    if (res != (s32_t)len)
        note("  -> %" PRId32 " (first byte %02x)", res, len ? io_buffer[0] : 0);
    if (!(fd->flags & SPIFFS_O_WRONLY)) {
        expect_result("write to read-only handle", res, SPIFFS_ERR_NOT_WRITABLE);
        return;
    }
    if (res == SPIFFS_ERR_FULL) {
        resync_after_full(fd->file, offset, io_buffer, len);
        if (file->exists)
            fd->offset = (uint32_t)SPIFFS_tell(&fs, fd->fh);
        return;
    }
    expect_result("write", res, (s32_t)len);
    if (len == 0)                           /* POSIX: "no other results" */
        return;
    if (offset > file->size)                /* gap left by another handle's truncation */
        memset(file->data + file->size, 0, offset - file->size);
    memcpy(file->data + offset, io_buffer, len);
    pending_add(&file->pending, offset, io_buffer, len);
    if (offset + len > file->size)
        file->size = offset + len;
    fd->offset = offset + len;
}

static void op_read(void) {
    model_fd_t *fd = random_open_fd();
    if (fd == NULL)
        return;
    model_file_t *file = &files[fd->file];
    uint32_t len = random_io_length();
    note("read fh %d len %u at %u (size %u)", fd->fh, len, fd->offset, file->size);
    s32_t res = SPIFFS_read(&fs, fd->fh, io_buffer, (s32_t)len);
    check_lock("read");
    if (lost_cache(fd, res))
        return;
    if (!(fd->flags & SPIFFS_O_RDONLY)) {
        expect_result("read from write-only handle", res, SPIFFS_ERR_NOT_READABLE);
        return;
    }
    if (fd->offset >= file->size) {
        expect_result("read at end", res, 0);     /* SPIFFS_read maps EOF to 0 */
        mark_durable(fd->file);
        return;
    }
    mark_durable(fd->file);                 /* the read flushed this file */
    uint32_t want = len < file->size - fd->offset ? len : file->size - fd->offset;
    expect_result("read", res, (s32_t)want);
    if (memcmp(io_buffer, file->data + fd->offset, want) != 0)
        fail("read fh %d: data differs from model at offset %u", fd->fh, fd->offset);
    fd->offset += want;
}

static void op_seek(void) {
    model_fd_t *fd = random_open_fd();
    if (fd == NULL)
        return;
    uint32_t size = files[fd->file].size;
    int whence = (int)below(3);
    s32_t offs = (s32_t)below(size + 600) - 300;
    s32_t base = whence == SPIFFS_SEEK_SET ? 0 : whence == SPIFFS_SEEK_CUR ? (s32_t)fd->offset
                                                                         : (s32_t)size;
    s32_t target = base + offs;
    note("seek fh %d %d whence %d -> %d (size %u)", fd->fh, offs, whence, target, size);
    s32_t res = SPIFFS_lseek(&fs, fd->fh, offs, whence);
    check_lock("lseek");
    if (lost_cache(fd, res))
        return;
    mark_durable(fd->file);
    if (target < 0) {
        expect_result("seek before start", res, SPIFFS_ERR_SEEK_BOUNDS);
    } else if ((uint32_t)target > size && !SPIFFS_POSIX_SEEK) {
        expect_result("seek beyond end", res, SPIFFS_ERR_END_OF_OBJECT);
        fd->offset = size;
    } else {
        expect_result("seek", res, target);
        fd->offset = (uint32_t)target;
    }
    s32_t tell = SPIFFS_tell(&fs, fd->fh);
    check_lock("tell");
    expect_result("tell", tell, (s32_t)fd->offset);
}

static void op_close(void) {
    model_fd_t *fd = random_open_fd();
    if (fd == NULL)
        return;
    note("close fh %d", fd->fh);
    s32_t res = SPIFFS_close(&fs, fd->fh);
    check_lock("close");
    if (lost_cache(fd, res))
        return;                 /* handle stays open; a second close frees it */
    expect_result("close", res, SPIFFS_OK);
    mark_durable(fd->file);
    fd->open = false;
    if (chance(30)) {
        note("close fh %d again", fd->fh);
        res = SPIFFS_close(&fs, fd->fh);
        check_lock("close again");
        if (res == SPIFFS_OK)
            fail("closing a closed handle succeeded");
    }
}

static void op_remove(void) {
    int slot = (int)below(MAX_FILES);
    int file = file_by_name(files[slot].name);
    note("remove '%s'", files[slot].name);
    s32_t res = SPIFFS_remove(&fs, files[slot].name);
    check_lock("remove");
    if (file < 0) {
        expect_result("remove missing file", res, SPIFFS_ERR_NOT_FOUND);
        return;
    }
    if (res == SPIFFS_ERR_FULL)
        return;
    expect_result("remove", res, SPIFFS_OK);
    files[file].exists = false;
    close_model_fds_of(file);
}

static void op_rename(void) {
    int from = (int)below(MAX_FILES), to = (int)below(MAX_FILES);
    int from_file = file_by_name(files[from].name);
    note("rename '%s' -> '%s'", files[from].name, files[to].name);
    s32_t res = SPIFFS_rename(&fs, files[from].name, files[to].name);
    check_lock("rename");
    if (from_file < 0) {
        expect_result("rename missing file", res, SPIFFS_ERR_NOT_FOUND);
        return;
    }
    if (file_by_name(files[to].name) >= 0) {
        expect_result("rename onto existing", res, SPIFFS_ERR_CONFLICTING_NAME);
        return;
    }
    if (res == SPIFFS_ERR_FULL)
        return;
    expect_result("rename", res, SPIFFS_OK);
    /* move the contents to slot 'to', and the handles with them */
    model_file_t moved = files[from_file];
    memcpy(moved.name, files[to].name, sizeof(moved.name));
    uint8_t *spare = files[to].data, *spare_durable = files[to].durable;
    pending_clear(&files[to].pending);
    files[to] = moved;                      /* takes over the pending writes */
    files[from_file].exists = false;
    files[from_file].data = spare;
    files[from_file].durable = spare_durable;
    memset(&files[from_file].pending, 0, sizeof(files[from_file].pending));
    make_name(from_file, files[from_file].name);
    for (int i = 0; i < MAX_FDS; i++)
        if (fds[i].open && fds[i].file == from_file)
            fds[i].file = to;
}

static void op_stat(void) {
    int slot = (int)below(MAX_FILES);
    int file = file_by_name(files[slot].name);
    spiffs_stat st;
    note("stat '%s'", files[slot].name);
    s32_t res = SPIFFS_stat(&fs, files[slot].name, &st);
    check_lock("stat");
    if (res == SPIFFS_ERR_FULL) {
        adopt_all_open_files();
        return;
    }
    mark_all_durable();
    if (file < 0) {
        expect_result("stat missing file", res, SPIFFS_ERR_NOT_FOUND);
        return;
    }
    expect_result("stat", res, SPIFFS_OK);
    if (st.size != files[file].size)
        fail("stat '%s' size %u, model %u", files[slot].name, st.size, files[file].size);
    if (strcmp((const char *)st.name, files[slot].name) != 0)
        fail("stat name '%s', expected '%s'", st.name, files[slot].name);
}

static void op_fstat(void) {
    model_fd_t *fd = random_open_fd();
    if (fd == NULL)
        return;
    spiffs_stat st;
    note("fstat fh %d", fd->fh);
    s32_t res = SPIFFS_fstat(&fs, fd->fh, &st);
    check_lock("fstat");
    if (lost_cache(fd, res))
        return;
    expect_result("fstat", res, SPIFFS_OK);
    mark_durable(fd->file);
    if (st.size != files[fd->file].size)
        fail("fstat fh %d size %u, model %u", fd->fh, st.size, files[fd->file].size);
}

static void op_flush(void) {
    model_fd_t *fd = random_open_fd();
    if (fd == NULL)
        return;
    note("fflush fh %d", fd->fh);
    s32_t res = SPIFFS_fflush(&fs, fd->fh);
    check_lock("fflush");
    if (lost_cache(fd, res))
        return;
    expect_result("fflush", res, SPIFFS_OK);
    mark_durable(fd->file);
}

static void op_too_long_name(void) {
    char name[SPIFFS_OBJ_NAME_LEN + 4];
    memset(name, 'N', SPIFFS_OBJ_NAME_LEN);
    name[SPIFFS_OBJ_NAME_LEN] = 0;
    note("open too-long name");
    s32_t res = SPIFFS_open(&fs, name, SPIFFS_O_CREAT | SPIFFS_O_RDWR, 0);
    check_lock("open long name");
    expect_result("open too-long name", res, SPIFFS_ERR_NAME_TOO_LONG);
}

static void op_remount(void) {
    note("remount");
    settle_caches();            /* SPIFFS_unmount cannot report a failed flush */
    SPIFFS_unmount(&fs);
    check_lock("unmount");
    for (int i = 0; i < MAX_FDS; i++)
        fds[i].open = false;
    expect_result("mount", mount(), SPIFFS_OK);
    check_lock("mount");
    verify_all();
}

static void op_check(void) {
    note("check");
    s32_t res = SPIFFS_check(&fs);
    check_lock("check");
    if (res == SPIFFS_ERR_FULL)
        adopt_all_open_files();
    else if (res != SPIFFS_OK)
        fail("SPIFFS_check returned %" PRId32, res);
    else
        mark_all_durable();
    verify_all();
}

static void op_gc(void) {
    note("gc_quick");
    s32_t res = SPIFFS_gc_quick(&fs, 0);
    check_lock("gc_quick");
    if (res == SPIFFS_ERR_FULL)
        adopt_all_open_files();
    if (res != SPIFFS_OK && res != SPIFFS_ERR_NO_DELETED_BLOCKS && res != SPIFFS_ERR_FULL)
        fail("gc_quick returned %" PRId32, res);
}

/* Map part of an open file's index into RAM (SPIFFS_IX_MAP), move the map,
 * or unmap it; reads must give the same data either way. */
static void op_map(void) {
    model_fd_t *fd = random_open_fd();
    if (fd == NULL)
        return;
    s32_t res;
    if (fd->mapped && chance(50)) {
        note("unmap fh %d", fd->fh);
        res = SPIFFS_ix_unmap(&fs, fd->fh);
        fd->mapped = false;
    } else if (fd->mapped) {
        uint32_t offset = below(files[fd->file].size + 1);
        note("remap fh %d at %u", fd->fh, offset);
        res = SPIFFS_ix_remap(&fs, fd->fh, offset);
    } else {
        uint32_t offset = chance(50) ? 0 : below(files[fd->file].size + 1);
        uint32_t entries = 1 + below(255);
        uint32_t len = (uint32_t)SPIFFS_ix_map_entries_to_bytes(&fs, entries - 1);
        note("map fh %d at %u for %u bytes", fd->fh, offset, len);
        res = SPIFFS_ix_map(&fs, fd->fh, &fd->map, offset, len, fd->map_buf);
        fd->mapped = res == SPIFFS_OK;
    }
    check_lock("ix map");
    if (lost_cache(fd, res))
        return;
    if (res != SPIFFS_OK)
        fail("index map operation returned %" PRId32, res);
}

typedef void (*operation_t)(void);

static const struct {
    operation_t run;
    uint32_t weight;
} operations[] = {
    { op_open, 14 }, { op_write, 30 }, { op_read, 20 }, { op_seek, 12 }, { op_close, 10 },
    { op_remove, 3 }, { op_rename, 3 }, { op_stat, 3 }, { op_fstat, 3 }, { op_flush, 3 },
    { op_too_long_name, 1 }, { op_remount, 1 }, { op_check, 1 }, { op_gc, 1 }, { op_map, 3 },
};

static void random_operation(void) {
    uint32_t total = 0;
    for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++)
        total += operations[i].weight;
    uint32_t pick = below(total);
    for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++) {
        if (pick < operations[i].weight) {
            operations[i].run();
            return;
        }
        pick -= operations[i].weight;
    }
}

/* ------------------------------------------------------------------ */
/* Seeds                                                               */
/* ------------------------------------------------------------------ */

static void start_seed(uint32_t seed) {
    current_seed = seed;
    rng_state = 0x9E3779B97F4A7C15ull ^ ((uint64_t)seed * 0x100000001B3ull);
    rnd();
    geo = geometries[below(sizeof(geometries) / sizeof(geometries[0]))];
    cache_pages = below(9);
    if (getenv("FUZZ_CACHE"))
        cache_pages = (uint32_t)atoi(getenv("FUZZ_CACHE"));
    nor_flash_free(&flash);
    nor_flash_init(&flash, geo.size, geo.erase);
    model_reset();
    history_next = 0;
    note("seed %u: page %u block %u erase %u size %u cache %u", seed, geo.page, geo.block,
         geo.erase, geo.size, cache_pages);
    s32_t res = mount();
    if (res != SPIFFS_ERR_NOT_A_FS)
        fail("mounting blank flash returned %" PRId32, res);
    SPIFFS_unmount(&fs);
    expect_result("format", SPIFFS_format(&fs), SPIFFS_OK);
    expect_result("mount", mount(), SPIFFS_OK);
    check_lock("mount");
}

static void run_seed(uint32_t seed, uint32_t op_count) {
    start_seed(seed);
    for (current_op = 0; current_op < op_count; current_op++) {
        random_operation();
        if (flash.bit_set_attempts != 0)
            fail("SPIFFS programmed a 0 bit back to 1");
    }
    settle_caches();            /* adopts files whose cached data is lost */
    for (int i = 0; i < MAX_FDS; i++)
        if (fds[i].open) {
            expect_result("final close", SPIFFS_close(&fs, fds[i].fh), SPIFFS_OK);
            fds[i].open = false;
        }
    op_remount();
    SPIFFS_unmount(&fs);
}

int main(int argc, char **argv) {
    uint32_t first = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 0) : 1;
    uint32_t count = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 200;
    uint32_t ops = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 0) : 2000;
    for (uint32_t seed = first; seed < first + count; seed++)
        run_seed(seed, ops);
    printf("fuzz: %u seeds x %u operations OK\n", count, ops);
    return 0;
}
