/*
 * pico_spiffs_errors.c - text and errno values for SPIFFS error codes.
 *
 * The errno values follow what POSIX specifies for the call that would
 * report them: e.g. writing to a read-only descriptor is EBADF, too many
 * open files is EMFILE, a negative seek is EINVAL.
 */
#include <errno.h>
#include <stddef.h>

#include "pico_spiffs.h"

typedef struct {
    int code;
    int posix_errno;
    const char *text;
} spiffs_error_t;

static const spiffs_error_t errors[] = {
    { SPIFFS_OK,                       0,            "no error" },
    { SPIFFS_ERR_NOT_MOUNTED,          ENODEV,       "not mounted" },
    { SPIFFS_ERR_FULL,                 ENOSPC,       "file system full" },
    { SPIFFS_ERR_NOT_FOUND,            ENOENT,       "file not found" },
    { SPIFFS_ERR_END_OF_OBJECT,        EINVAL,       "end of file" },
    { SPIFFS_ERR_DELETED,              ENOENT,       "deleted" },
    { SPIFFS_ERR_NOT_FINALIZED,        EIO,          "page not finalised" },
    { SPIFFS_ERR_NOT_INDEX,            EIO,          "not an index page" },
    { SPIFFS_ERR_OUT_OF_FILE_DESCS,    EMFILE,       "too many open files" },
    { SPIFFS_ERR_FILE_CLOSED,          EBADF,        "file closed" },
    { SPIFFS_ERR_FILE_DELETED,         EBADF,        "file deleted" },
    { SPIFFS_ERR_BAD_DESCRIPTOR,       EBADF,        "bad file descriptor" },
    { SPIFFS_ERR_IS_INDEX,             EIO,          "page is an index page" },
    { SPIFFS_ERR_IS_FREE,              EIO,          "page is free" },
    { SPIFFS_ERR_INDEX_SPAN_MISMATCH,  EIO,          "index span mismatch" },
    { SPIFFS_ERR_DATA_SPAN_MISMATCH,   EIO,          "data span mismatch" },
    { SPIFFS_ERR_INDEX_REF_FREE,       EIO,          "index refers to a free page" },
    { SPIFFS_ERR_INDEX_REF_LU,         EIO,          "index refers to a lookup page" },
    { SPIFFS_ERR_INDEX_REF_INVALID,    EIO,          "index refers to an invalid page" },
    { SPIFFS_ERR_INDEX_FREE,           EIO,          "index page is free" },
    { SPIFFS_ERR_INDEX_LU,             EIO,          "index page is a lookup page" },
    { SPIFFS_ERR_INDEX_INVALID,        EIO,          "index invalid" },
    { SPIFFS_ERR_NOT_WRITABLE,         EBADF,        "not open for writing" },
    { SPIFFS_ERR_NOT_READABLE,         EBADF,        "not open for reading" },
    { SPIFFS_ERR_CONFLICTING_NAME,     EEXIST,       "name already in use" },
    { SPIFFS_ERR_NOT_CONFIGURED,       ENODEV,       "flash not configured or not found" },
    { SPIFFS_ERR_NOT_A_FS,             ENODEV,       "no file system (format needed)" },
    { SPIFFS_ERR_MOUNTED,              EBUSY,        "mounted" },
    { SPIFFS_ERR_ERASE_FAIL,           EIO,          "flash erase failed" },
    { SPIFFS_ERR_MAGIC_NOT_POSSIBLE,   EINVAL,       "geometry leaves no room for the magic number" },
    { SPIFFS_ERR_NO_DELETED_BLOCKS,    ENOSPC,       "no blocks to reclaim" },
    { SPIFFS_ERR_FILE_EXISTS,          EEXIST,       "file exists" },
    { SPIFFS_ERR_NOT_A_FILE,           EISDIR,       "not a file" },
    { SPIFFS_ERR_RO_NOT_IMPL,          EROFS,        "read-only build" },
    { SPIFFS_ERR_RO_ABORTED_OPERATION, EROFS,        "read-only build" },
    { SPIFFS_ERR_PROBE_TOO_FEW_BLOCKS, ENODEV,       "too few blocks" },
    { SPIFFS_ERR_PROBE_NOT_A_FS,       ENODEV,       "no file system found" },
    { SPIFFS_ERR_NAME_TOO_LONG,        ENAMETOOLONG, "name too long" },
    { SPIFFS_ERR_IX_MAP_UNMAPPED,      EINVAL,       "index map not mapped" },
    { SPIFFS_ERR_IX_MAP_MAPPED,        EINVAL,       "index map already mapped" },
    { SPIFFS_ERR_IX_MAP_BAD_RANGE,     EINVAL,       "index map range" },
    { SPIFFS_ERR_SEEK_BOUNDS,          EINVAL,       "seek before start of file" },
    { SPIFFS_ERR_BAD_CONFIG,           EINVAL,       "volume settings unusable (size, offset or geometry)" },
    { SPIFFS_ERR_INTERNAL,             EIO,          "flash access failed" },
    { SPIFFS_ERR_TEST,                 EIO,          "test error" },
};

static const spiffs_error_t unknown = { 0, EIO, "unknown error" };

static const spiffs_error_t *find_error(int code) {
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++)
        if (errors[i].code == code)
            return &errors[i];
    return &unknown;
}

const char *pico_spiffs_error_text(int spiffs_error) {
    return find_error(spiffs_error)->text;
}

int pico_spiffs_errno(int spiffs_error) {
    return find_error(spiffs_error)->posix_errno;
}
