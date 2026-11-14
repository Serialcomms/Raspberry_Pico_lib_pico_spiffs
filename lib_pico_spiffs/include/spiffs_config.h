/*
 * spiffs_config.h - SPIFFS build configuration for pico_spiffs.
 *
 * Tuned for a Raspberry Pi Pico volume holding a few large files. Every
 * setting can be overridden from CMake with target_compile_definitions().
 * The volume geometry (page, block, size, address) is not set here but in
 * pico_spiffs.h, so it can differ between onboard and external flash.
 */
#ifndef SPIFFS_CONFIG_H_
#define SPIFFS_CONFIG_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef int32_t s32_t;
typedef uint32_t u32_t;
typedef int16_t s16_t;
typedef uint16_t u16_t;
typedef int8_t s8_t;
typedef uint8_t u8_t;

/* --- Index widths --------------------------------------------------------
 * 16-bit indexes keep RAM and flash overhead low. They limit a volume to
 * 65,535 logical pages (0xFFFF marks a free page): 16 MB of 256-byte pages
 * or 32 MB of 512-byte pages, less one page. A file is limited to 65,535
 * data pages. SPIFFS_mount() rejects
 * a volume that would overflow them.
 */
typedef u16_t spiffs_block_ix;
typedef u16_t spiffs_page_ix;
typedef u16_t spiffs_obj_id;
typedef u16_t spiffs_span_ix;

/* --- Debug output: off ---------------------------------------------------
 * To trace SPIFFS, define e.g. SPIFFS_DBG(...) as printf(__VA_ARGS__).
 */
#define _SPIPRIi   "%d"
#define _SPIPRIad  "%08x"
#define _SPIPRIbl  "%04x"
#define _SPIPRIpg  "%04x"
#define _SPIPRIsp  "%04x"
#define _SPIPRIfd  "%d"
#define _SPIPRIid  "%04x"
#define _SPIPRIfl  "%02x"
#ifndef SPIFFS_DBG
#define SPIFFS_DBG(...)
#endif
#ifndef SPIFFS_GC_DBG
#define SPIFFS_GC_DBG(...)
#endif
#ifndef SPIFFS_CACHE_DBG
#define SPIFFS_CACHE_DBG(...)
#endif
#ifndef SPIFFS_CHECK_DBG
#define SPIFFS_CHECK_DBG(...)
#endif
#ifndef SPIFFS_API_DBG
#define SPIFFS_API_DBG(...)
#endif

/* --- Features ---------------------------------------------------------- */
#ifndef SPIFFS_BUFFER_HELP
#define SPIFFS_BUFFER_HELP              0
#endif
#ifndef SPIFFS_CACHE
#define SPIFFS_CACHE                    1   /* RAM cache of flash pages */
#endif
#ifndef SPIFFS_CACHE_WR
#define SPIFFS_CACHE_WR                 1   /* gathers small writes into pages */
#endif
#ifndef SPIFFS_CACHE_STATS
#define SPIFFS_CACHE_STATS              0
#endif
#ifndef SPIFFS_PAGE_CHECK
#define SPIFFS_PAGE_CHECK               1   /* verify page headers on access */
#endif
#ifndef SPIFFS_GC_MAX_RUNS
#define SPIFFS_GC_MAX_RUNS              5
#endif
#ifndef SPIFFS_GC_STATS
#define SPIFFS_GC_STATS                 0
#endif
#ifndef SPIFFS_GC_HEUR_W_DELET
#define SPIFFS_GC_HEUR_W_DELET          (5)
#endif
#ifndef SPIFFS_GC_HEUR_W_USED
#define SPIFFS_GC_HEUR_W_USED           (-1)
#endif
#ifndef SPIFFS_GC_HEUR_W_ERASE_AGE
#define SPIFFS_GC_HEUR_W_ERASE_AGE      (50)
#endif

/* Longest file name, including its terminating NUL. */
#ifndef SPIFFS_OBJ_NAME_LEN
#define SPIFFS_OBJ_NAME_LEN             (32)
#endif
#ifndef SPIFFS_OBJ_META_LEN
#define SPIFFS_OBJ_META_LEN             (0)
#endif
#ifndef SPIFFS_COPY_BUFFER_STACK
#define SPIFFS_COPY_BUFFER_STACK        (256)
#endif

/* Magic number: a volume is recognised only if its size and geometry match. */
#define SPIFFS_USE_MAGIC                (1)
#define SPIFFS_USE_MAGIC_LENGTH         (1)

/* Several volumes are not needed (one flash device per build), but keeping
 * the configuration in the spiffs struct costs nothing and allows tests to
 * try many geometries. */
#define SPIFFS_SINGLETON                0
#define SPIFFS_ALIGNED_OBJECT_INDEX_TABLES 1
#define SPIFFS_HAL_CALLBACK_EXTRA       0

/* File handles start at 3, after stdin, stdout and stderr, so the POSIX
 * layer can hand SPIFFS handles straight to newlib as file descriptors. */
#define SPIFFS_FILEHDL_OFFSET           1

#define SPIFFS_READ_ONLY                0
#define SPIFFS_TEMPORAL_FD_CACHE        0   /* out of scope: many small files */
#ifndef SPIFFS_IX_MAP
#define SPIFFS_IX_MAP                   1   /* fast seeks in large files */
#endif
/* Remember where each open file's object index pages are, for this many
 * index pages (each covers about 125 KB of a file with 512-byte pages):
 * random access in a large file then needs no scan of the whole volume. */
#ifndef SPIFFS_OBJIX_CACHE
#define SPIFFS_OBJIX_CACHE              32
#endif
/* Read flag bytes before clearing bits in them, rather than writing 0xFF
 * over the other bits and relying on the flash to AND them. Costs a small
 * read; makes every write exact, which the host tests check. */
#define SPIFFS_NO_BLIND_WRITES          1

/* lseek beyond the end of a file is allowed, as in POSIX. */
#define SPIFFS_POSIX_SEEK               1
#define SPIFFS_SECURE_ERASE             0
#define SPIFFS_TEST_VISUALISATION       0

/* --- Locking -------------------------------------------------------------
 * pico_spiffs serialises every SPIFFS call with a recursive mutex, so the
 * file system may be used from both cores. Host tests define their own.
 */
#ifndef SPIFFS_LOCK
struct spiffs_t;
void pico_spiffs_lock(struct spiffs_t *fs);
void pico_spiffs_unlock(struct spiffs_t *fs);
#define SPIFFS_LOCK(fs)   pico_spiffs_lock(fs)
#define SPIFFS_UNLOCK(fs) pico_spiffs_unlock(fs)
#endif

#endif /* SPIFFS_CONFIG_H_ */
