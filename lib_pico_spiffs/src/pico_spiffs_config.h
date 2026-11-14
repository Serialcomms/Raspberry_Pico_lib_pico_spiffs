/*
 * pico_spiffs_config.h - volume settings and their defaults.
 *
 * Override any of these with target_compile_definitions() in CMake.
 */
#ifndef PICO_SPIFFS_CONFIG_H
#define PICO_SPIFFS_CONFIG_H

#include "pico.h"                 /* board settings: PICO_FLASH_SIZE_BYTES */

#include "pico_spiffs_flash.h"

/* --- Where the volume lives --------------------------------------------- */

#if PICO_SPIFFS_FLASH_EXTERNAL
/* Whole external chip by default; 0 means "to the end of the chip". */
#ifndef PICO_SPIFFS_FLASH_OFFSET
#define PICO_SPIFFS_FLASH_OFFSET 0u
#endif
#ifndef PICO_SPIFFS_SIZE
#define PICO_SPIFFS_SIZE 0u
#endif
#else
/* Onboard: 1 MB at the end of flash by default, after the program, but
 * short of the last 4 KB sector. For RP2350-A2 chips the SDK puts an extra
 * UF2 block (the RP2350-E10 workaround) at 0x10FFFF00, the end of a 16 MB
 * flash, and loading a UF2 writes it there: inside the volume, it would
 * damage the file system each time a new program is loaded. */
#ifndef PICO_SPIFFS_SIZE
#define PICO_SPIFFS_SIZE (1024u * 1024u)
#endif
#ifndef PICO_SPIFFS_E10_RESERVE
#define PICO_SPIFFS_E10_RESERVE 4096u
#endif
/* ...and starting on a block boundary, so whole blocks can be erased with the
 * flash's 64 KB block erase. */
#ifndef PICO_SPIFFS_FLASH_OFFSET
#define PICO_SPIFFS_FLASH_OFFSET \
    ((PICO_FLASH_SIZE_BYTES - PICO_SPIFFS_E10_RESERVE - PICO_SPIFFS_SIZE) / \
     PICO_SPIFFS_BLOCK_SIZE * PICO_SPIFFS_BLOCK_SIZE)
#endif
#endif

/* --- Geometry ------------------------------------------------------------
 * Logical page: the unit SPIFFS allocates and indexes. Logical block: the
 * unit it garbage-collects, a multiple of the 4 KB erase sector.
 */
#ifndef PICO_SPIFFS_PAGE_SIZE
#define PICO_SPIFFS_PAGE_SIZE 512u
#endif
#ifndef PICO_SPIFFS_BLOCK_SIZE
#define PICO_SPIFFS_BLOCK_SIZE (64u * 1024u)
#endif

/* --- RAM ------------------------------------------------------------------ */

#ifndef PICO_SPIFFS_MAX_OPEN_FILES
#define PICO_SPIFFS_MAX_OPEN_FILES 8
#endif
#ifndef PICO_SPIFFS_CACHE_PAGES
#define PICO_SPIFFS_CACHE_PAGES 4
#endif

/* First file descriptor: 0, 1 and 2 are stdin, stdout and stderr. */
#define PICO_SPIFFS_FIRST_FD 3

_Static_assert(PICO_SPIFFS_BLOCK_SIZE % PICO_SPIFFS_ERASE_SIZE == 0,
               "PICO_SPIFFS_BLOCK_SIZE must be a multiple of 4096");
_Static_assert(PICO_SPIFFS_BLOCK_SIZE % PICO_SPIFFS_PAGE_SIZE == 0,
               "PICO_SPIFFS_BLOCK_SIZE must be a multiple of PICO_SPIFFS_PAGE_SIZE");
_Static_assert(PICO_SPIFFS_FLASH_OFFSET % PICO_SPIFFS_ERASE_SIZE == 0,
               "PICO_SPIFFS_FLASH_OFFSET must be a multiple of 4096");
_Static_assert(PICO_SPIFFS_CACHE_PAGES <= 32, "SPIFFS uses at most 32 cache pages");

#endif
