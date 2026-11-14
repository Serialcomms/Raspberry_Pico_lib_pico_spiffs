/*
 * xfer_test.c - a large file arriving in small pieces, as from an XMODEM
 * or ZMODEM receiver: a 4 MB volume, files of 2 to 3.5 MB, written 128,
 * 1024 or 8192 bytes at a time. Every write() and close() result is
 * checked; the report gives the volume's capacity, the first failing
 * write, the final file size and whether the contents read back intact.
 *
 * Built like posix_test.c (see the Makefile), on both backends:
 *   make xfer
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "host_sdk.h"
#include "pico_spiffs.h"
#include "pico_spiffs_config.h"

int pico_open(const char *path, int oflag, ...);
int pico_close(int fd);
int pico_read(int fd, char *buffer, int length);
int pico_write(int fd, char *buffer, int length);
int pico_stat(const char *path, struct stat *st);
int pico_unlink(const char *path);

typedef struct {
    long written;       /* bytes accepted by write() */
    long fail_at;       /* offset of the first failing write, or -1 */
    int fail_errno;
    int close_errno;    /* 0 if close() succeeded */
    long final_size;    /* st_size after close() */
    long bad_bytes;     /* bytes missing or reading back wrong */
} xfer_result_t;

static int failures;

/* --- Helpers ---------------------------------------------------------- */

static uint8_t pattern_byte(long offset) {
    return (uint8_t)((offset * 7) ^ (offset >> 11));
}

static void fill_pattern(uint8_t *buffer, long offset, int length) {
    for (int i = 0; i < length; i++)
        buffer[i] = pattern_byte(offset + i);
}

static long file_size(const char *path) {
    struct stat st;
    return pico_stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static void fresh_volume(void) {
    pico_spiffs_unmount();
    host_flash_reset(0xFF);
    pico_spiffs_mount(PICO_SPIFFS_FORMAT_IF_BLANK);
}

static void print_info(const char *when) {
    uint32_t total = 0, used = 0;
    pico_spiffs_info(&total, &used);
    printf("    %-7s total %7u  used %7u  free %7u\n", when, (unsigned)total,
           (unsigned)used, (unsigned)(total - used));
}

/* --- One transfer ----------------------------------------------------- */

static void receive_file(const char *path, int flags, long size, int chunk,
                         xfer_result_t *r) {
    static uint8_t buffer[8192];
    memset(r, 0, sizeof(*r));
    r->fail_at = -1;
    int fd = pico_open(path, flags, 0644);
    if (fd < 0) {
        r->fail_at = 0;
        r->fail_errno = errno;
        return;
    }
    for (long done = 0; done < size; done += chunk) {
        int n = size - done < chunk ? (int)(size - done) : chunk;
        fill_pattern(buffer, done, n);
        int got = pico_write(fd, (char *)buffer, n);
        if (got != n) {
            r->fail_at = done;
            r->fail_errno = got < 0 ? errno : 0;
            break;
        }
        r->written += got;
    }
    if (pico_close(fd) != 0)
        r->close_errno = errno;
}

static long count_bad_bytes(const char *path, long size) {
    static uint8_t buffer[4096];
    int fd = pico_open(path, O_RDONLY);
    if (fd < 0)
        return size;
    long bad = 0, at = 0;
    int n;
    while (at < size && (n = pico_read(fd, (char *)buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < n && at + i < size; i++)
            if (buffer[i] != pattern_byte(at + i))
                bad++;
        at += n;
    }
    pico_close(fd);
    return bad + (at < size ? size - at : 0);
}

static void finish_result(const char *path, xfer_result_t *r) {
    r->final_size = file_size(path);
    r->bad_bytes = count_bad_bytes(path, r->written);
}

static void print_result(const char *label, long size, const xfer_result_t *r) {
    printf("    %-34s want %7ld  wrote %7ld  size %7ld  bad %ld", label, size,
           r->written, r->final_size, r->bad_bytes);
    if (r->fail_at >= 0)
        printf("  FAILED at %ld: %s", r->fail_at, strerror(r->fail_errno));
    if (r->close_errno)
        printf("; close: %s", strerror(r->close_errno));
    printf("\n");
}

/* Cached bytes that cannot be stored are lost, which POSIX allows if a later
 * write() or close() reports it. Losing data with no error at all, or
 * storing wrong bytes, is a bug. */
static void judge(const xfer_result_t *r) {
    long stored = r->final_size < 0 ? 0 : r->final_size;
    long missing = stored < r->written ? r->written - stored : 0;
    int reported = r->fail_at >= 0 || r->close_errno != 0;
    if ((missing && !reported) || r->bad_bytes > missing) {
        printf("      ** BUG: data lost without an error, or stored wrongly\n");
        failures++;
    }
}

/* --- Scenarios -------------------------------------------------------- */

static void test_single_transfer(long size, int chunk) {
    char label[64];
    xfer_result_t r;
    fresh_volume();
    receive_file("rx.bin", O_WRONLY | O_CREAT | O_TRUNC, size, chunk, &r);
    finish_result("rx.bin", &r);
    snprintf(label, sizeof(label), "%4.2f MB in %d-byte writes", size / 1048576.0, chunk);
    print_result(label, size, &r);
    judge(&r);
}

/* The same file sent again over an existing copy. */
static void test_resend(long size, int chunk, int flags, const char *how) {
    xfer_result_t r;
    fresh_volume();
    receive_file("rx.bin", O_WRONLY | O_CREAT | O_TRUNC, size, chunk, &r);
    print_info("1st:");
    receive_file("rx.bin", flags, size, chunk, &r);
    finish_result("rx.bin", &r);
    print_result(how, size, &r);
    judge(&r);
}

/* Received to a new name while the old copy is still there. */
static void test_new_name_beside_old(long size, int chunk) {
    xfer_result_t r;
    fresh_volume();
    receive_file("old.bin", O_WRONLY | O_CREAT | O_TRUNC, size, chunk, &r);
    receive_file("new.bin", O_WRONLY | O_CREAT | O_TRUNC, size, chunk, &r);
    finish_result("new.bin", &r);
    print_result("new name, old copy kept", size, &r);
    judge(&r);
}

/* Fill the volume with big writes, then leave a small write in the cache,
 * so close() itself cannot store it. Returns the handle, or -1. */
static int fill_then_close(const char *path, int *close_errno) {
    static char block[4096];
    memset(block, 0x55, sizeof(block));
    int fd = pico_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    while (pico_write(fd, block, sizeof(block)) == (int)sizeof(block))
        ;
    pico_write(fd, block, 100);
    *close_errno = pico_close(fd) == 0 ? 0 : errno;
    return fd;
}

/* A close() that fails to store cached data must still free the handle:
 * closing it a second time must fail with EBADF. */
static void test_failed_close_releases_handle(void) {
    int close_errno = 0;
    fresh_volume();
    int fd = fill_then_close("fill.bin", &close_errno);
    int again = fd < 0 ? 0 : pico_close(fd);
    int again_errno = errno;
    int released = again == -1 && again_errno == EBADF;
    printf("    full-volume close: %s; handle %s\n",
           close_errno ? strerror(close_errno) : "no error",
           released ? "released" : "still open");
    if (!released) {
        printf("      ** BUG: a failed close() leaks its file handle\n");
        failures++;
    }
}

static void run_single_transfers(void) {
    static const long sizes[] = { 2000000, 2500000, 3000000, 3300000, 3500000 };
    static const int chunks[] = { 128, 1024, 8192 };
    printf("  one file onto an empty volume\n");
    fresh_volume();
    print_info("empty:");
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++)
            test_single_transfer(sizes[s], chunks[c]);
}

static void run_resends(void) {
    const long size = 2500000;
    printf("  the same 2.5 MB file received twice (128-byte writes)\n");
    test_resend(size, 128, O_WRONLY | O_CREAT | O_TRUNC, "again with O_TRUNC");
    test_resend(size, 128, O_WRONLY | O_CREAT, "again without O_TRUNC (overwrite)");
    test_new_name_beside_old(size, 128);
}

static void run_full_volume_close(void) {
    printf("  close() on a full volume\n");
    test_failed_close_releases_handle();
}

int main(void) {
    printf("pico_spiffs transfer tests (%s flash, %u KB volume, %u-byte pages, "
           "%u KB blocks)\n", PICO_SPIFFS_FLASH_EXTERNAL ? "external W25Q" : "onboard",
           PICO_SPIFFS_SIZE / 1024u, PICO_SPIFFS_PAGE_SIZE, PICO_SPIFFS_BLOCK_SIZE / 1024u);
    run_single_transfers();
    run_resends();
    run_full_volume_close();
    printf("%d bugs found\n", failures);
    return failures == 0 ? 0 : 1;
}
