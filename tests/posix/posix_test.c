/*
 * posix_test.c - tests of pico_spiffs through the C library interface:
 * open() flags against the Open Group specification, errno values,
 * stat, rename, unlink, directories, truncation, a full volume, mounting
 * and formatting, and a large file. Built twice: on the onboard backend
 * and on the external (W25Q) backend, over the simulated chips in
 * host_sdk.c, which also check every flash operation the backend makes.
 *
 * The library's POSIX functions are renamed for the host build (see the
 * Makefile), e.g. open -> pico_open, so they do not replace the host's.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dirent.h"
#include "host_sdk.h"
#include "pico_spiffs.h"
#include "pico_spiffs_config.h"

/* The library's system calls, as renamed for the host build. */
int pico_open(const char *path, int oflag, ...);
int pico_close(int fd);
int pico_read(int fd, char *buffer, int length);
int pico_write(int fd, char *buffer, int length);
off_t pico_lseek(int fd, off_t offset, int whence);
int pico_fstat(int fd, struct stat *st);
int pico_stat(const char *path, struct stat *st);
int pico_isatty(int fd);
int pico_unlink(const char *path);
int pico_link(const char *existing, const char *new_path);

#define VOLUME_OFFSET PICO_SPIFFS_FLASH_OFFSET

static int failures, checks;

#define CHECK(cond) check((cond), #cond, __LINE__)
/* The call is made before errno is read (one statement, then the next). */
#define CHECK_ERR(call, err) do { \
        long result_ = (long)(call); \
        int errno_ = errno; \
        check_errno(result_ == -1, errno_, (err), #call, __LINE__); \
    } while (0)

static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL line %d: %s\n", line, what);
    }
}

static void check_errno(int failed, int got, int want, const char *what, int line) {
    checks++;
    if (!failed || got != want) {
        failures++;
        printf("  FAIL line %d: %s: %s, errno %d (%s), wanted %d (%s)\n", line, what,
               failed ? "failed" : "succeeded", got, strerror(got), want, strerror(want));
    }
}

/* --- Helpers ---------------------------------------------------------- */

static int write_file(const char *path, const char *text) {
    int fd = pico_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return -1;
    int n = pico_write(fd, (char *)text, (int)strlen(text));
    pico_close(fd);
    return n;
}

static long file_size(const char *path) {
    struct stat st;
    return pico_stat(path, &st) == 0 ? (long)st.st_size : -1;
}

/* Read a whole (small) file into buffer as a string. */
static int read_file(const char *path, char *buffer, int size) {
    int fd = pico_open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    int n = pico_read(fd, buffer, size - 1);
    pico_close(fd);
    buffer[n < 0 ? 0 : n] = '\0';
    return n;
}

static void fresh_volume(void) {
    pico_spiffs_unmount();
    host_flash_reset(0xFF);
}

/* --- Tests ------------------------------------------------------------ */

static void test_mount_on_first_use(void) {
    printf("first use of blank flash formats it\n");
    fresh_volume();
    CHECK(!pico_spiffs_mounted());
    CHECK(write_file("hello.txt", "hello") == 5);
    CHECK(pico_spiffs_mounted());
    char text[16];
    CHECK(read_file("hello.txt", text, sizeof(text)) == 5 && strcmp(text, "hello") == 0);
}

static void test_open_flags(void) {
    printf("open() flags\n");
    fresh_volume();
    CHECK_ERR(pico_open("missing", O_RDONLY), ENOENT);
    CHECK_ERR(pico_open("missing", O_WRONLY), ENOENT);
    CHECK(write_file("f", "0123456789") == 10);

    /* O_EXCL with O_CREAT: fails on an existing file, works on a new one */
    CHECK_ERR(pico_open("f", O_WRONLY | O_CREAT | O_EXCL), EEXIST);
    int fd = pico_open("new", O_WRONLY | O_CREAT | O_EXCL);
    CHECK(fd >= PICO_SPIFFS_FIRST_FD);
    pico_close(fd);
    /* O_EXCL without O_CREAT: no effect */
    fd = pico_open("f", O_RDONLY | O_EXCL);
    CHECK(fd >= 0);
    pico_close(fd);

    /* O_TRUNC: only with write access */
    fd = pico_open("f", O_RDONLY | O_TRUNC);
    CHECK(fd >= 0);
    pico_close(fd);
    CHECK(file_size("f") == 10);
    fd = pico_open("f", O_WRONLY | O_TRUNC);
    pico_close(fd);
    CHECK(file_size("f") == 0);

    /* O_APPEND: every write at the end, wherever the offset is */
    CHECK(write_file("a", "abc") == 3);
    fd = pico_open("a", O_RDWR | O_APPEND);
    CHECK(pico_lseek(fd, 0, SEEK_SET) == 0);
    CHECK(pico_write(fd, "def", 3) == 3);
    pico_close(fd);
    char text[16];
    read_file("a", text, sizeof(text));
    CHECK(strcmp(text, "abcdef") == 0);

    /* access modes */
    CHECK_ERR(pico_open("f", O_ACCMODE), EINVAL);
    fd = pico_open("f", O_RDONLY);
    CHECK_ERR(pico_write(fd, "x", 1), EBADF);
    pico_close(fd);
    fd = pico_open("f", O_WRONLY);
    CHECK_ERR(pico_read(fd, text, 1), EBADF);
    pico_close(fd);

    /* directories */
    CHECK_ERR(pico_open("/", O_RDONLY), EISDIR);
    CHECK_ERR(pico_open("f", O_RDONLY | O_DIRECTORY), ENOTDIR);
    CHECK_ERR(pico_open("", O_RDONLY), ENOENT);

    /* O_SYNC (no RAM cache) and ignored flags */
    fd = pico_open("sync", O_WRONLY | O_CREAT | O_SYNC | O_NONBLOCK | O_NOCTTY);
    CHECK(fd >= 0 && pico_write(fd, "synced", 6) == 6);
    CHECK(file_size("sync") == 6);          /* visible before close */
    pico_close(fd);
}

static void test_read_write_seek(void) {
    printf("read, write and lseek\n");
    fresh_volume();
    write_file("f", "0123456789");
    char buffer[32];
    int fd = pico_open("f", O_RDWR);
    CHECK(pico_read(fd, buffer, 4) == 4 && memcmp(buffer, "0123", 4) == 0);
    CHECK(pico_lseek(fd, 0, SEEK_CUR) == 4);
    CHECK(pico_lseek(fd, -2, SEEK_END) == 8);
    CHECK(pico_read(fd, buffer, 10) == 2 && memcmp(buffer, "89", 2) == 0);
    CHECK(pico_read(fd, buffer, 10) == 0);              /* end of file */
    CHECK_ERR(pico_lseek(fd, -1, SEEK_SET), EINVAL);
    CHECK_ERR(pico_lseek(fd, 0, 7), EINVAL);
    CHECK(pico_write(fd, buffer, 0) == 0);              /* writes nothing, harms nothing */
    CHECK(file_size("f") == 10);

    /* beyond the end: reads give end of file, a write fills the gap with zeros */
    CHECK(pico_lseek(fd, 15, SEEK_SET) == 15);
    CHECK(pico_read(fd, buffer, 4) == 0);
    CHECK(pico_write(fd, "X", 1) == 1);
    CHECK(file_size("f") == 16);
    CHECK(pico_lseek(fd, 10, SEEK_SET) == 10);
    CHECK(pico_read(fd, buffer, 6) == 6 && memcmp(buffer, "\0\0\0\0\0X", 6) == 0);
    pico_close(fd);

    CHECK_ERR(pico_lseek(1, 0, SEEK_SET), ESPIPE);
    CHECK_ERR(pico_close(fd), EBADF);                   /* already closed */
    CHECK_ERR(pico_close(99), EBADF);
    CHECK_ERR(pico_read(99, buffer, 1), EBADF);
}

static void test_console_descriptors(void) {
    printf("stdin, stdout and stderr\n");
    struct stat st;
    CHECK(pico_fstat(1, &st) == 0 && S_ISCHR(st.st_mode));
    CHECK(pico_isatty(0) == 1 && pico_isatty(2) == 1);
    host_stdout_length = 0;
    CHECK(pico_write(1, "out\n", 4) == 4 && strcmp(host_stdout, "out\n") == 0);
    CHECK_ERR(pico_write(0, "x", 1), EBADF);
    CHECK(pico_close(2) == 0);
    int fd = pico_open("tty?", O_WRONLY | O_CREAT);
    CHECK(pico_isatty(fd) == 0 && errno == ENOTTY);
    pico_close(fd);
}

static void test_stat_and_names(void) {
    printf("stat and names\n");
    fresh_volume();
    write_file("/a.txt", "abc");
    struct stat st;
    CHECK(pico_stat("a.txt", &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 3);
    CHECK(pico_stat("./a.txt", &st) == 0 && st.st_size == 3);
    CHECK(pico_stat("/", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK_ERR(pico_stat("missing", &st), ENOENT);
    CHECK_ERR(pico_stat("", &st), ENOENT);
    int fd = pico_open("a.txt", O_RDONLY);
    CHECK(pico_fstat(fd, &st) == 0 && st.st_size == 3 && st.st_nlink == 1);
    pico_close(fd);

    char longest[SPIFFS_OBJ_NAME_LEN] = "", too_long[SPIFFS_OBJ_NAME_LEN + 1] = "";
    memset(longest, 'n', sizeof(longest) - 1);
    memset(too_long, 'n', sizeof(too_long) - 1);
    CHECK(write_file(longest, "x") == 1);
    CHECK_ERR(pico_open(too_long, O_WRONLY | O_CREAT), ENAMETOOLONG);
    CHECK_ERR(pico_stat(too_long, &st), ENAMETOOLONG);
}

static void test_unlink_rename(void) {
    printf("unlink, rename, link\n");
    fresh_volume();
    write_file("a", "aaa");
    write_file("b", "bb");
    CHECK(pico_unlink("a") == 0 && file_size("a") == -1);
    CHECK_ERR(pico_unlink("a"), ENOENT);
    CHECK_ERR(pico_unlink("/"), EPERM);

    CHECK(rename("b", "c") == 0 && file_size("b") == -1 && file_size("c") == 2);
    write_file("d", "dddd");
    CHECK(rename("c", "d") == 0);                       /* replaces the target */
    CHECK(file_size("c") == -1 && file_size("d") == 2);
    CHECK(rename("d", "d") == 0 && file_size("d") == 2);
    CHECK_ERR(rename("missing", "e"), ENOENT);
    CHECK_ERR(rename("missing", "d"), ENOENT);
    CHECK(file_size("d") == 2);                         /* target kept */
    CHECK_ERR(rename("d", "/"), EBUSY);
    CHECK_ERR(pico_link("d", "e"), EMLINK);
}

static void test_directories(void) {
    printf("directories\n");
    fresh_volume();
    const char *names[] = { "one", "two", "three" };
    for (int i = 0; i < 3; i++)
        write_file(names[i], "x");
    DIR *dir = opendir("/");
    CHECK(dir != NULL);
    int seen = 0;
    struct dirent *e;
    while (dir && (e = readdir(dir)) != NULL) {
        CHECK(e->d_type == DT_REG);
        for (int i = 0; i < 3; i++)
            if (strcmp(e->d_name, names[i]) == 0)
                seen |= 1 << i;
    }
    CHECK(seen == 7);
    rewinddir(dir);
    CHECK(readdir(dir) != NULL && telldir(dir) == 1);
    seekdir(dir, 2);
    CHECK(telldir(dir) == 2 && readdir(dir) != NULL && readdir(dir) == NULL);
    CHECK(closedir(dir) == 0);
    CHECK(opendir(".") != NULL);
    CHECK(opendir("one") == NULL && errno == ENOTDIR);
    CHECK(opendir("nowhere") == NULL && errno == ENOENT);

    CHECK_ERR(mkdir("/", 0777), EEXIST);
    CHECK_ERR(mkdir("sub", 0777), ENOSYS);
    CHECK_ERR(rmdir("/"), EBUSY);
    CHECK_ERR(rmdir("one"), ENOTDIR);
    CHECK_ERR(rmdir("nowhere"), ENOENT);
}

static void test_truncate_and_sync(void) {
    printf("ftruncate, truncate, fsync\n");
    fresh_volume();
    write_file("f", "0123456789");
    int fd = pico_open("f", O_RDWR);
    CHECK(pico_lseek(fd, 3, SEEK_SET) == 3);
    CHECK(ftruncate(fd, 5) == 0 && file_size("f") == 5);
    CHECK(ftruncate(fd, 8) == 0 && file_size("f") == 8);    /* extends with zeros */
    CHECK(pico_lseek(fd, 0, SEEK_CUR) == 3);                 /* offset unchanged */
    char buffer[16];
    CHECK(pico_lseek(fd, 0, SEEK_SET) == 0 && pico_read(fd, buffer, 16) == 8);
    CHECK(memcmp(buffer, "01234\0\0\0", 8) == 0);
    CHECK(fsync(fd) == 0);
    CHECK_ERR(ftruncate(fd, -1), EINVAL);
    pico_close(fd);
    fd = pico_open("f", O_RDONLY);
    CHECK_ERR(ftruncate(fd, 2), EBADF);
    pico_close(fd);
    CHECK(truncate("f", 1) == 0 && file_size("f") == 1);
    CHECK_ERR(truncate("missing", 1), ENOENT);

    fd = pico_open("g", O_WRONLY | O_CREAT | O_APPEND);
    CHECK(ftruncate(fd, 100) == 0 && file_size("g") == 100);  /* append handle too */
    pico_close(fd);
}

static void test_open_file_limit(void) {
    printf("too many open files\n");
    fresh_volume();
    int fds[PICO_SPIFFS_MAX_OPEN_FILES];
    for (int i = 0; i < PICO_SPIFFS_MAX_OPEN_FILES; i++) {
        char name[16];
        snprintf(name, sizeof(name), "f%d", i);
        fds[i] = pico_open(name, O_WRONLY | O_CREAT);
        CHECK(fds[i] >= 0);
    }
    CHECK_ERR(pico_open("one-more", O_WRONLY | O_CREAT), EMFILE);
    for (int i = 0; i < PICO_SPIFFS_MAX_OPEN_FILES; i++)
        pico_close(fds[i]);
}

static void test_full_volume(void) {
    printf("a full volume\n");
    fresh_volume();
    write_file("keep", "kept");
    static char chunk[4096];
    memset(chunk, 0x42, sizeof(chunk));
    int fd = pico_open("fill", O_WRONLY | O_CREAT);
    long written = 0;
    int n;
    while ((n = pico_write(fd, chunk, sizeof(chunk))) == (int)sizeof(chunk))
        written += n;
    CHECK(n == -1 && errno == ENOSPC);
    pico_close(fd);
    CHECK(written > PICO_SPIFFS_SIZE / 2);
    CHECK(pico_unlink("fill") == 0);
    fd = pico_open("again", O_WRONLY | O_CREAT);
    CHECK(pico_write(fd, chunk, sizeof(chunk)) == (int)sizeof(chunk));
    pico_close(fd);
    char text[8];
    CHECK(read_file("keep", text, sizeof(text)) == 4 && strcmp(text, "kept") == 0);
    CHECK(pico_spiffs_check() == SPIFFS_OK);
}

static void test_mount_and_format(void) {
    printf("mount, remount, format\n");
    fresh_volume();
    write_file("persist", "still here");
    pico_spiffs_unmount();
    CHECK(pico_spiffs_mount(PICO_SPIFFS_MOUNT_ONLY) == SPIFFS_OK);
    char text[16];
    CHECK(read_file("persist", text, sizeof(text)) == 10);
    CHECK(pico_spiffs_format() == SPIFFS_OK && pico_spiffs_mounted());
    CHECK(file_size("persist") == -1);

    /* flash holding something else is never formatted unless asked */
    pico_spiffs_unmount();
    uint8_t *volume = host_volume_bytes(VOLUME_OFFSET, PICO_SPIFFS_FLASH_EXTERNAL);
    memset(volume, 0x5A, PICO_SPIFFS_SIZE);
    CHECK(pico_spiffs_mount(PICO_SPIFFS_FORMAT_IF_BLANK) == SPIFFS_ERR_NOT_A_FS);
    CHECK(volume[100] == 0x5A);
    struct stat st;
    CHECK_ERR(pico_stat("x", &st), ENODEV);            /* auto-mount refuses too */
    CHECK(volume[100] == 0x5A);
    CHECK(pico_spiffs_mount(PICO_SPIFFS_FORMAT_IF_NEEDED) == SPIFFS_OK);
    CHECK(write_file("x", "x") == 1);
    host_counts.bit_set_attempts = 0;       /* the garbage was not erased flash */
}

static void test_large_file(void) {
    printf("a large file\n");
    fresh_volume();
    const long size = PICO_SPIFFS_SIZE / 2;
    static uint8_t chunk[3000], back[3000];
    int fd = pico_open("big", O_WRONLY | O_CREAT);
    for (long done = 0; done < size; done += (long)sizeof(chunk)) {
        for (size_t i = 0; i < sizeof(chunk); i++)
            chunk[i] = (uint8_t)((done + (long)i) * 7);
        long n = size - done < (long)sizeof(chunk) ? size - done : (long)sizeof(chunk);
        CHECK(pico_write(fd, (char *)chunk, (int)n) == n);
    }
    pico_close(fd);
    CHECK(file_size("big") == size);
    fd = pico_open("big", O_RDWR);
    srand(1);
    int bad = 0;
    for (int i = 0; i < 300; i++) {
        long at = rand() % (size - (long)sizeof(back));
        pico_lseek(fd, at, SEEK_SET);
        if (pico_read(fd, (char *)back, sizeof(back)) != (int)sizeof(back))
            bad++;
        for (size_t j = 0; j < sizeof(back); j++)
            if (back[j] != (uint8_t)((at + (long)j) * 7))
                bad++;
    }
    CHECK(bad == 0);
    pico_close(fd);
}

static void run(void (*test)(void)) {
    test();
    if (host_counts.bit_set_attempts != 0) {
        printf("  FAIL: %u attempts to program a 0 bit back to 1\n", host_counts.bit_set_attempts);
        failures++;
        host_counts.bit_set_attempts = 0;
    }
}

static unsigned all_erases(void) {
    return host_counts.erases + host_counts.erases_64k;
}

/* Write a file of whole blocks, then remove it, leaving deleted blocks. */
static void leave_deleted_blocks(const char *name, int blocks) {
    static char chunk[4096];
    memset(chunk, 0x33, sizeof(chunk));
    int fd = pico_open(name, O_WRONLY | O_CREAT);
    for (long done = 0; done < (long)blocks * PICO_SPIFFS_BLOCK_SIZE; done += sizeof(chunk))
        pico_write(fd, chunk, sizeof(chunk));
    pico_close(fd);
    CHECK(pico_unlink(name) == 0);
}

static void test_gc(void) {
    printf("erasing deleted blocks ahead of time\n");
    fresh_volume();
    CHECK(write_file("keep", "kept") == 4);
    CHECK(pico_spiffs_gc(0) == 0);                  /* nothing deleted yet */
    leave_deleted_blocks("old", 4);

    unsigned erases = all_erases();
    int erased = pico_spiffs_gc(1);
    CHECK(erased == 1 && all_erases() > erases);
    erased = pico_spiffs_gc(0);
    CHECK(erased >= 2);                             /* the rest of the file */
    CHECK(pico_spiffs_gc(0) == 0);

    char text[8] = "";
    CHECK(read_file("keep", text, sizeof(text)) == 4 && strcmp(text, "kept") == 0);
    erases = all_erases();
    leave_deleted_blocks("new", 2);                 /* erased blocks were ready */
    CHECK(all_erases() == erases);
    CHECK(pico_spiffs_check() == SPIFFS_OK);

    pico_spiffs_unmount();
    CHECK(pico_spiffs_gc(0) == SPIFFS_ERR_NOT_MOUNTED);
}

int main(void) {
    printf("pico_spiffs POSIX tests (%s flash)\n",
           PICO_SPIFFS_FLASH_EXTERNAL ? "external W25Q" : "onboard");
    run(test_mount_on_first_use);
    run(test_open_flags);
    run(test_read_write_seek);
    run(test_console_descriptors);
    run(test_stat_and_names);
    run(test_unlink_rename);
    run(test_directories);
    run(test_truncate_and_sync);
    run(test_open_file_limit);
    run(test_full_volume);
    run(test_mount_and_format);
    run(test_large_file);
    run(test_gc);
    CHECK(host_counts.rule_breaks == 0);
    CHECK(host_counts.bit_set_attempts == 0);
    printf("%d checks, %d failed; flash: %u programs, %u erases\n", checks, failures,
           host_counts.programs, host_counts.erases);
    return failures == 0 ? 0 : 1;
}
