/*
 * main.c - pico_spiffs example: ordinary C file functions on a SPIFFS
 * volume, then a large-file speed test that checks every byte read back.
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico_spiffs.h"

#define BIG_FILE "disk.img"
#define BIG_FILE_MAX (2u * 1024u * 1024u)
#define CHUNK 4096u

static void show_flash(void) {
    uint32_t baud = pico_spiffs_spi_baud();
    if (baud == 0)
        printf("flash: onboard\n");
    else
        printf("flash: external, SPI clock %lu.%03lu MHz\n",
               (unsigned long)(baud / 1000000), (unsigned long)(baud / 1000 % 1000));
}

static void mount_volume(void) {
    int res = pico_spiffs_mount(PICO_SPIFFS_FORMAT_IF_BLANK);
    printf("mount: %s\n", pico_spiffs_error_text(res));
    if (res == SPIFFS_ERR_NOT_A_FS) {
        printf("flash holds something else: formatting\n");
        res = pico_spiffs_format();
        printf("format: %s\n", pico_spiffs_error_text(res));
    }
}

static void write_and_read_text(void) {
    FILE *f = fopen("hello.txt", "w");
    if (f == NULL) {
        printf("fopen: %s\n", strerror(errno));
        return;
    }
    fprintf(f, "Hello from pico_spiffs, uptime %lu ms\n",
            (unsigned long)to_ms_since_boot(get_absolute_time()));
    fclose(f);

    char line[80] = "";
    f = fopen("hello.txt", "r");
    if (f != NULL && fgets(line, sizeof(line), f) != NULL)
        printf("read back: %s", line);
    if (f != NULL)
        fclose(f);
}

static void show_posix_errors(void) {
    FILE *f = fopen("hello.txt", "wx");     /* O_EXCL: must fail, file exists */
    printf("fopen(\"hello.txt\", \"wx\"): %s\n", f ? "opened (wrong)" : strerror(errno));
    if (f)
        fclose(f);
    printf("fopen of a missing file: %s\n",
           fopen("missing.txt", "r") ? "opened (wrong)" : strerror(errno));
}

static void list_directory(void) {
    DIR *dir = opendir("/");
    struct dirent *entry;
    struct stat st;
    printf("files:\n");
    while (dir != NULL && (entry = readdir(dir)) != NULL) {
        stat(entry->d_name, &st);
        printf("  %-32s %8ld bytes\n", entry->d_name, (long)st.st_size);
    }
    if (dir)
        closedir(dir);
}

/* Half the volume, at most 2 MB, in whole chunks. */
static uint32_t big_file_bytes(void) {
    uint32_t total = 0;
    pico_spiffs_info(&total, NULL);
    uint32_t bytes = total / 2 < BIG_FILE_MAX ? total / 2 : BIG_FILE_MAX;
    return bytes - bytes % CHUNK;
}

/* A byte that depends on its file offset, so a shifted, repeated or
 * dropped page shows up as a mismatch (a constant fill would hide it). */
static uint8_t pattern_byte(uint32_t offset) {
    uint32_t page = offset / 256;
    return (uint8_t)(offset ^ (page * 167u) ^ (page >> 8));
}

static void fill_chunk(uint8_t *chunk, uint32_t offset) {
    for (uint32_t i = 0; i < CHUNK; i++)
        chunk[i] = pattern_byte(offset + i);
}

/* Write the file; returns the bytes written, stopping at the first error. */
static uint32_t write_big_file(uint32_t bytes) {
    static uint8_t chunk[CHUNK];
    FILE *f = fopen(BIG_FILE, "w");
    if (f == NULL) {
        printf("fopen: %s\n", strerror(errno));
        return 0;
    }
    uint32_t done = 0;
    while (done < bytes) {
        fill_chunk(chunk, done);
        if (fwrite(chunk, 1, CHUNK, f) != CHUNK)
            break;
        done += CHUNK;
    }
    if (done < bytes)
        printf("write stopped at %lu bytes: %s\n", (unsigned long)done, strerror(errno));
    if (fclose(f) != 0)
        printf("fclose: %s\n", strerror(errno));
    return done;
}

typedef struct {
    uint32_t bytes;                 /* bytes read */
    uint32_t mismatches;            /* bytes that differ from the pattern */
    uint32_t first_bad;             /* offset of the first mismatch */
} read_result_t;

static void check_chunk(const uint8_t *chunk, size_t got, read_result_t *result) {
    for (size_t i = 0; i < got; i++) {
        uint32_t offset = result->bytes + (uint32_t)i;
        if (chunk[i] == pattern_byte(offset))
            continue;
        if (result->mismatches++ == 0)
            result->first_bad = offset;
    }
}

/* Read the file back, comparing every byte with the pattern. */
static read_result_t read_big_file(void) {
    static uint8_t chunk[CHUNK];
    read_result_t result = {0, 0, 0};
    FILE *f = fopen(BIG_FILE, "r");
    if (f == NULL) {
        printf("fopen: %s\n", strerror(errno));
        return result;
    }
    size_t got;
    while ((got = fread(chunk, 1, CHUNK, f)) > 0) {
        check_chunk(chunk, got, &result);
        result.bytes += (uint32_t)got;
    }
    fclose(f);
    return result;
}

static void report_check(const read_result_t *result, uint32_t written) {
    if (result->bytes != written)
        printf("verify: read %lu bytes, expected %lu\n",
               (unsigned long)result->bytes, (unsigned long)written);
    if (result->mismatches == 0)
        printf("verify: all %lu bytes match\n", (unsigned long)result->bytes);
    else
        printf("verify: %lu bytes differ, first at offset %lu (0x%lx)\n",
               (unsigned long)result->mismatches, (unsigned long)result->first_bad,
               (unsigned long)result->first_bad);
}

/* Remove the file from an earlier run, so the write below times a new
 * file. SPIFFS only marks the old pages deleted here. */
static void remove_big_file(void) {
    absolute_time_t start = get_absolute_time();
    int res = remove(BIG_FILE);
    int64_t us = absolute_time_diff_us(start, get_absolute_time());
    if (res == 0)
        printf("remove old %s: %lld ms\n", BIG_FILE, (long long)(us / 1000));
    else if (errno != ENOENT)
        printf("remove %s: %s\n", BIG_FILE, strerror(errno));
}

/* Erase the blocks the removed file left, as a program would when idle,
 * so the write below doesn't have to. */
static void erase_deleted_blocks(void) {
    absolute_time_t start = get_absolute_time();
    int erased = pico_spiffs_gc(0);
    int64_t us = absolute_time_diff_us(start, get_absolute_time());
    if (erased < 0)
        printf("gc: %s\n", pico_spiffs_error_text(erased));
    else
        printf("erase %d deleted blocks: %lld ms\n", erased, (long long)(us / 1000));
}

static void timed_big_file(void) {
    uint32_t bytes = big_file_bytes();
    remove_big_file();
    erase_deleted_blocks();
    absolute_time_t start = get_absolute_time();
    uint32_t written = write_big_file(bytes);
    int64_t write_us = absolute_time_diff_us(start, get_absolute_time());

    start = get_absolute_time();
    read_result_t result = read_big_file();
    int64_t read_us = absolute_time_diff_us(start, get_absolute_time());

    printf("%lu KB: write %lld ms (%lu KB/s), read and verify %lld ms\n",
           (unsigned long)(written / 1024), (long long)(write_us / 1000),
           (unsigned long)(write_us > 0 ? (uint64_t)written * 1000 / (uint64_t)write_us : 0),
           (long long)(read_us / 1000));
    report_check(&result, written);
}

/* Walk the whole volume and check its structure (takes a few seconds). */
static void check_volume(void) {
    absolute_time_t start = get_absolute_time();
    int res = pico_spiffs_check();
    int64_t us = absolute_time_diff_us(start, get_absolute_time());
    printf("check: %s (%lld ms)\n", pico_spiffs_error_text(res), (long long)(us / 1000));
}

/* Close the volume, say goodbye, and restart in BOOTSEL mode ready for the
   next UF2. The pause lets a USB terminal receive the message first. */
static void reboot_to_bootsel(void) {
    pico_spiffs_unmount();
    printf("\nExample finished: rebooting to BOOTSEL\n");
    stdio_flush();
    sleep_ms(500);
    reset_usb_boot(0, 0);
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);                 /* time to open a USB terminal */
    printf("\npico_spiffs example\n");
    mount_volume();
    show_flash();
    write_and_read_text();
    show_posix_errors();
    timed_big_file();
    check_volume();
    list_directory();
    reboot_to_bootsel();
}
