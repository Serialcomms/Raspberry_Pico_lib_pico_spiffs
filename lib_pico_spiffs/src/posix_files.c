/*
 * posix_files.c - file descriptor system calls for newlib: open, close,
 * read, write, lseek, fstat, isatty, plus fsync, ftruncate and truncate.
 *
 * These replace the SDK's weak stubs (pico_clib_interface), so open(),
 * fopen(), fread(), fprintf() and the rest work on the SPIFFS volume.
 * Descriptors 0, 1 and 2 still go to the console.
 *
 * open() flags, after the Open Group specification of open():
 *   O_RDONLY, O_WRONLY, O_RDWR   access mode (any other value: EINVAL)
 *   O_CREAT                      create the file if it does not exist
 *   O_EXCL                       with O_CREAT: fail with EEXIST if it does
 *   O_TRUNC                      with write access: truncate to 0 bytes
 *   O_APPEND                     every write goes to the end of the file
 *   O_SYNC, O_DSYNC, O_RSYNC     writes bypass the RAM cache
 *   O_NONBLOCK, O_NOCTTY         no effect on regular files
 *   O_DIRECTORY                  ENOTDIR: the only directory is "/"
 * POSIX leaves O_EXCL without O_CREAT, and O_TRUNC with O_RDONLY, undefined;
 * here they have no effect.
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "posix_internal.h"
#include "posix_stdio.h"

#define STDIN_FD 0

/* ------------------------------------------------------------------ */
/* open() flags                                                        */
/* ------------------------------------------------------------------ */

/* SPIFFS access flags for an O_ACCMODE value, or 0 if it is not valid. */
static spiffs_flags access_flags(int oflag) {
    switch (oflag & O_ACCMODE) {
    case O_RDONLY: return SPIFFS_O_RDONLY;
    case O_WRONLY: return SPIFFS_O_WRONLY;
    case O_RDWR:   return SPIFFS_O_RDWR;
    default:       return 0;
    }
}

static bool has_write_access(int oflag) {
    return (oflag & O_ACCMODE) == O_WRONLY || (oflag & O_ACCMODE) == O_RDWR;
}

static spiffs_flags option_flags(int oflag) {
    spiffs_flags flags = 0;
    if (oflag & O_CREAT)
        flags |= SPIFFS_O_CREAT;
    if ((oflag & O_CREAT) && (oflag & O_EXCL))
        flags |= SPIFFS_O_EXCL;
    if ((oflag & O_TRUNC) && has_write_access(oflag))
        flags |= SPIFFS_O_TRUNC;
    if (oflag & O_APPEND)
        flags |= SPIFFS_O_APPEND;
    if (oflag & (O_SYNC | O_DSYNC | O_RSYNC))
        flags |= SPIFFS_O_DIRECT;
    return flags;
}

/* ------------------------------------------------------------------ */
/* Descriptors                                                         */
/* ------------------------------------------------------------------ */

int _open(const char *path, int oflag, ...) {
    if (!posix_volume_ready())
        return -1;
    if (path != NULL && posix_is_root(path))
        return posix_fail_errno(EISDIR);
    const char *name = posix_spiffs_name(path);
    if (name == NULL)
        return -1;
    if (oflag & O_DIRECTORY)
        return posix_fail_errno(ENOTDIR);
    spiffs_flags access = access_flags(oflag);
    if (access == 0)
        return posix_fail_errno(EINVAL);
    spiffs_file fd = SPIFFS_open(pico_spiffs_fs(), name, access | option_flags(oflag), 0);
    if (fd < 0)
        return posix_fail(fd);
    posix_map_if_large(fd);
    return fd;
}

int _close(int fd) {
    if (is_stdio_fd(fd))
        return 0;
    posix_unmap(fd);
    s32_t res = SPIFFS_close(pico_spiffs_fs(), (spiffs_file)fd);
    return res < 0 ? posix_fail(res) : 0;
}

int _read(int fd, char *buffer, int length) {
    if (fd == STDIN_FD)
        return posix_stdio_read(buffer, length);
    if (is_stdio_fd(fd))
        return posix_fail_errno(EBADF);
    if (length < 0)
        return posix_fail_errno(EINVAL);
    s32_t got = SPIFFS_read(pico_spiffs_fs(), (spiffs_file)fd, buffer, length);
    return got < 0 ? posix_fail(got) : (int)got;
}

int _write(int fd, char *buffer, int length) {
    if (fd == STDIN_FD)
        return posix_fail_errno(EBADF);
    if (is_stdio_fd(fd))
        return posix_stdio_write(buffer, length);
    if (length < 0)
        return posix_fail_errno(EINVAL);
    s32_t done = SPIFFS_write(pico_spiffs_fs(), (spiffs_file)fd, buffer, length);
    return done < 0 ? posix_fail(done) : (int)done;
}

off_t _lseek(int fd, off_t offset, int whence) {
    if (is_stdio_fd(fd))
        return posix_fail_errno(ESPIPE);
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
        return posix_fail_errno(EINVAL);
    s32_t res = SPIFFS_lseek(pico_spiffs_fs(), (spiffs_file)fd, (s32_t)offset, whence);
    return res < 0 ? posix_fail(res) : (off_t)res;
}

/* ------------------------------------------------------------------ */
/* stat                                                                */
/* ------------------------------------------------------------------ */

/* Fill in a struct stat for a file (used by stat() in posix_names.c too). */
void posix_fill_stat(struct stat *st, const spiffs_stat *s) {
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    st->st_ino = s->obj_id;
    st->st_nlink = 1;
    st->st_size = (off_t)s->size;
    st->st_blksize = PICO_SPIFFS_PAGE_SIZE;
    st->st_blocks = (s->size + 511) / 512;
}

int _fstat(int fd, struct stat *st) {
    if (is_stdio_fd(fd)) {
        /* a character device, so newlib line-buffers stdout */
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR | S_IRUSR | S_IWUSR;
        return 0;
    }
    spiffs_stat s;
    s32_t res = SPIFFS_fstat(pico_spiffs_fs(), (spiffs_file)fd, &s);
    if (res < 0)
        return posix_fail(res);
    posix_fill_stat(st, &s);
    return 0;
}

int _isatty(int fd) {
    if (is_stdio_fd(fd))
        return 1;
    errno = ENOTTY;
    return 0;
}

/* ------------------------------------------------------------------ */
/* fsync, ftruncate, truncate                                          */
/* ------------------------------------------------------------------ */

int fsync(int fd) {
    if (is_stdio_fd(fd))
        return posix_fail_errno(EINVAL);
    s32_t res = SPIFFS_fflush(pico_spiffs_fs(), (spiffs_file)fd);
    return res < 0 ? posix_fail(res) : 0;
}

/* Extend a file with zeros to length, leaving its offset where it was.
 * Writing at the end works for O_APPEND handles too. */
static s32_t extend_with_zeros(spiffs *fs, spiffs_file fd, u32_t size, u32_t length) {
    s32_t offset = SPIFFS_tell(fs, fd);
    if (offset < 0)
        return offset;
    s32_t res = SPIFFS_lseek(fs, fd, 0, SPIFFS_SEEK_END);
    uint8_t zeros[64];
    memset(zeros, 0, sizeof(zeros));
    while (res >= 0 && size < length) {
        u32_t len = length - size < sizeof(zeros) ? length - size : (u32_t)sizeof(zeros);
        res = SPIFFS_write(fs, fd, zeros, (s32_t)len);
        size += len;
    }
    s32_t back = SPIFFS_lseek(fs, fd, offset, SPIFFS_SEEK_SET);
    return res < 0 ? res : back < 0 ? back : SPIFFS_OK;
}

static s32_t set_length(spiffs *fs, spiffs_file fd, u32_t length) {
    spiffs_stat s;
    s32_t res = SPIFFS_fstat(fs, fd, &s);
    if (res < 0)
        return res;
    if (length < s.size)
        return SPIFFS_ftruncate(fs, fd, length);
    if (length > s.size)
        return extend_with_zeros(fs, fd, s.size, length);
    return SPIFFS_OK;
}

int ftruncate(int fd, off_t length) {
    if (is_stdio_fd(fd))
        return posix_fail_errno(EINVAL);
    if (length < 0)
        return posix_fail_errno(EINVAL);
    spiffs *fs = pico_spiffs_fs();
    pico_spiffs_lock(fs);
    s32_t res = set_length(fs, (spiffs_file)fd, (u32_t)length);
    pico_spiffs_unlock(fs);
    return res < 0 ? posix_fail(res) : 0;
}

int truncate(const char *path, off_t length) {
    int fd = _open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    int res = ftruncate(fd, length);
    int saved_errno = errno;
    _close(fd);
    errno = saved_errno;
    return res;
}
