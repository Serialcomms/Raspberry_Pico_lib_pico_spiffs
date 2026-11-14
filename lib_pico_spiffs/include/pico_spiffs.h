/*
 * pico_spiffs.h - SPIFFS flash file system for Raspberry Pi Pico boards.
 *
 * One volume, on the Pico's own flash or on an external Winbond W25Q chip
 * (chosen at build time with PICO_SPIFFS_FLASH in CMake). Mount it once
 * with pico_spiffs_mount(); after that the C library's own file functions
 * (fopen, fread, fprintf, stat, rename, remove, opendir, ...) work on it,
 * so existing programs need no changes. The SPIFFS API itself is available
 * too, through pico_spiffs_fs().
 *
 * Settings, all optional, as compile definitions (see the README):
 *   PICO_SPIFFS_SIZE            volume size in bytes
 *   PICO_SPIFFS_FLASH_OFFSET    where it starts in the flash chip
 *   PICO_SPIFFS_MAX_OPEN_FILES  files open at once
 *   PICO_SPIFFS_CACHE_PAGES     RAM cache, in logical pages
 *   PICO_SPIFFS_LED_READ_PIN, _WRITE_PIN, _ERASE_PIN   activity LEDs
 */
#ifndef PICO_SPIFFS_H
#define PICO_SPIFFS_H

#include <stdbool.h>
#include <stdint.h>

#include "spiffs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What pico_spiffs_mount() should do with flash that holds no volume (or
 * a volume of a different size or geometry). */
typedef enum {
    PICO_SPIFFS_MOUNT_ONLY,         /* fail with SPIFFS_ERR_NOT_A_FS */
    PICO_SPIFFS_FORMAT_IF_BLANK,    /* format only erased (all 0xFF) flash */
    PICO_SPIFFS_FORMAT_IF_NEEDED,   /* format whatever is there, then mount */
} pico_spiffs_mount_mode_t;

/*! \brief Mount the volume, formatting it first if asked and necessary
 *
 * Safe to call when already mounted. Returns SPIFFS_OK (0) or a negative
 * SPIFFS error code; pico_spiffs_error_text() describes it.
 *
 * PICO_SPIFFS_FORMAT_IF_BLANK is the safe choice for a first run: it never
 * erases data, such as a volume made with different settings, which then
 * gives SPIFFS_ERR_NOT_A_FS until pico_spiffs_format() is called.
 */
int pico_spiffs_mount(pico_spiffs_mount_mode_t mode);

/*! \brief Erase the volume and leave it mounted and empty
 *
 * Closes any open files first. No unmount/format/mount sequence is needed:
 * this does all three.
 */
int pico_spiffs_format(void);

/*! \brief Close every open file and unmount the volume */
void pico_spiffs_unmount(void);

bool pico_spiffs_mounted(void);

/*! \brief Volume size and bytes in use (SPIFFS_OK or an error) */
int pico_spiffs_info(uint32_t *total_bytes, uint32_t *used_bytes);

/*! \brief Check the volume and repair what it can (slow) */
int pico_spiffs_check(void);

/*! \brief Erase blocks that hold only deleted pages, ahead of time
 *
 * Removing or truncating a file only marks its pages deleted; the blocks
 * are erased when a later write needs the space, which slows that write.
 * Call this when the program is idle (for example after removing a large
 * file) so the next write finds erased blocks ready. No live data is moved.
 *
 * Each block takes one block erase (about 150-300 ms on a W25Q, less
 * onboard), and other file calls can run between blocks.
 *
 * \param max_blocks the most blocks to erase in this call (0: all of them)
 * \return the number of blocks erased, or a negative SPIFFS error
 */
int pico_spiffs_gc(uint32_t max_blocks);

/*! \brief The SPIFFS volume, for calling the SPIFFS API directly */
spiffs *pico_spiffs_fs(void);

/*! \brief Text for a SPIFFS error code, e.g. "file system full" */
const char *pico_spiffs_error_text(int spiffs_error);

/*! \brief The errno value (ENOENT, ENOSPC, ...) for a SPIFFS error code */
int pico_spiffs_errno(int spiffs_error);

/*! \brief Print the files on the volume to stdout, with sizes */
void pico_spiffs_list(void);

/*! \brief The SPI clock in use for external flash, in Hz (0 for onboard flash,
 *  or before the first mount) */
uint32_t pico_spiffs_spi_baud(void);

#ifdef __cplusplus
}
#endif

#endif /* PICO_SPIFFS_H */
