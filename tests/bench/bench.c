/*
 * bench.c - flash work done by pico_spiffs for a few large files, for
 * choosing the default page size, block size and cache.
 *
 * Runs on the external (W25Q) backend over the simulated chip in
 * tests/posix/host_sdk.c, through the POSIX layer, and prints the flash
 * operations of each workload with a time estimate from the W25Q64JV
 * datasheet's typical figures. Built once per configuration (bench.sh).
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
long pico_lseek(int fd, long offset, int whence);

#define FILE_BYTES (1536u * 1024u)
#define CHUNK 4096u
#define SECTOR 512u
#define RANDOM_OPS 2000

/* W25Q64JV typical timings */
#define PAGE_PROGRAM_MS 0.4
#define ERASE_4K_MS 45.0
#define ERASE_64K_MS 150.0
#define SPI_BYTES_PER_MS 5000.0         /* 40 MHz */
#define READ_COMMAND_MS 0.001

static char buffer[CHUNK];

static double estimate_ms(const host_counts_t *c) {
    return c->programs * PAGE_PROGRAM_MS + c->erases * ERASE_4K_MS +
           c->erases_64k * ERASE_64K_MS + c->bytes_read / SPI_BYTES_PER_MS +
           (c->bytes_programmed / SPI_BYTES_PER_MS) + c->reads * READ_COMMAND_MS;
}

static void report(const char *workload, double bytes) {
    host_counts_t *c = &host_counts;
    double ms = estimate_ms(c);
    printf("  %-22s %8.0f ms %7.1f KB/s  reads %7u (%6.0f KB)  programs %6u  erases %4u+%u\n",
           workload, ms, bytes / 1024.0 / (ms / 1000.0), c->reads, c->bytes_read / 1024.0,
           c->programs, c->erases, c->erases_64k);
    memset(c, 0, sizeof(*c));
}

static void write_file(const char *name) {
    int fd = pico_open(name, O_WRONLY | O_CREAT | O_TRUNC);
    for (uint32_t done = 0; done < FILE_BYTES; done += CHUNK) {
        memset(buffer, (int)(done / CHUNK), CHUNK);
        if (pico_write(fd, buffer, CHUNK) != (int)CHUNK) {
            printf("write failed at %u (errno %d)\n", done, errno);
            exit(1);
        }
    }
    pico_close(fd);
}

static void read_file(const char *name) {
    int fd = pico_open(name, O_RDONLY);
    for (uint32_t done = 0; done < FILE_BYTES; done += CHUNK)
        if (pico_read(fd, buffer, CHUNK) != (int)CHUNK || buffer[0] != (char)(done / CHUNK)) {
            printf("read failed at %u\n", done);
            exit(1);
        }
    pico_close(fd);
}

static spiffs_page_ix map_buffer[16384];

static void random_sectors(const char *name, int writing) {
    int fd = pico_open(name, O_RDWR);
    if (getenv("BENCH_MAP")) {
        static spiffs_ix_map map;
        s32_t res = SPIFFS_ix_map(pico_spiffs_fs(), (spiffs_file)fd, &map, 0, FILE_BYTES, map_buffer);
        if (res != SPIFFS_OK)
            printf("ix_map: %s\n", pico_spiffs_error_text(res));
    }
    srand(42);
    for (int i = 0; i < RANDOM_OPS; i++) {
        long at = (long)(rand() % (FILE_BYTES / SECTOR)) * SECTOR;
        pico_lseek(fd, at, 0);
        int n = writing ? pico_write(fd, buffer, SECTOR) : pico_read(fd, buffer, SECTOR);
        if (n != (int)SECTOR) {
            printf("%s failed\n", writing ? "write" : "read");
            exit(1);
        }
    }
    pico_close(fd);
}

int main(void) {
    printf("page %u, block %u KB, cache %u pages\n", PICO_SPIFFS_PAGE_SIZE,
           PICO_SPIFFS_BLOCK_SIZE / 1024, PICO_SPIFFS_CACHE_PAGES);
    host_flash_reset(0xFF);
    int formatted = pico_spiffs_format();
    if (formatted != SPIFFS_OK) {
        printf("format: %s\n", pico_spiffs_error_text(formatted));
        return 1;
    }
    memset(&host_counts, 0, sizeof(host_counts));
    write_file("disk1.img");
    write_file("disk2.img");
    report("write 2 x 1.5 MB", 2.0 * FILE_BYTES);
    read_file("disk1.img");
    read_file("disk2.img");
    report("read 2 x 1.5 MB", 2.0 * FILE_BYTES);
    random_sectors("disk1.img", 1);
    report("2000 random 512 B wr", RANDOM_OPS * SECTOR);
    random_sectors("disk1.img", 0);
    report("2000 random 512 B rd", RANDOM_OPS * SECTOR);
    return 0;
}
