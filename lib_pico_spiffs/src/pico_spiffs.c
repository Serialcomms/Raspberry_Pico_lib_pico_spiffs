/*
 * pico_spiffs.c - the volume: buffers, configuration, mount and format.
 *
 * SPIFFS can only format a volume after a mount attempt has given it the
 * configuration, and only while unmounted, so formatting usually means
 * "mount, unmount, format, mount again". pico_spiffs_mount() and
 * pico_spiffs_format() do that sequence internally.
 */
#include "pico/mutex.h"

#include "pico_spiffs.h"
#include "pico_spiffs_config.h"
#include "pico_spiffs_flash.h"
#include "spiffs_nucleus.h"     /* sizeof(spiffs_fd), spiffs_cache */

static spiffs volume;
static spiffs_config config;
static bool flash_ready;

/* Buffers SPIFFS works in; uint32_t arrays keep them 4-byte aligned. */
#define WORDS(bytes) (((bytes) + 3u) / 4u)
static uint32_t work_buffer[WORDS(2u * PICO_SPIFFS_PAGE_SIZE)];
static uint32_t fd_buffer[WORDS(PICO_SPIFFS_MAX_OPEN_FILES * sizeof(spiffs_fd))];
static uint32_t cache_buffer[WORDS(sizeof(spiffs_cache) + PICO_SPIFFS_CACHE_PAGES *
                                   (sizeof(spiffs_cache_page) + PICO_SPIFFS_PAGE_SIZE))];

/* ------------------------------------------------------------------ */
/* Locking: every SPIFFS call holds this mutex, so both cores may use  */
/* the file system.                                                    */
/* ------------------------------------------------------------------ */

auto_init_recursive_mutex(volume_mutex);

void pico_spiffs_lock(struct spiffs_t *fs) {
    (void)fs;
    recursive_mutex_enter_blocking(&volume_mutex);
}

void pico_spiffs_unlock(struct spiffs_t *fs) {
    (void)fs;
    recursive_mutex_exit(&volume_mutex);
}

/* ------------------------------------------------------------------ */
/* Erasing                                                             */
/* ------------------------------------------------------------------ */

/* True if a 4 KB sector already reads as erased (all 0xFF). Used blocks give
 * themselves away in the first bytes, so this is usually a very short read. */
static bool sector_blank(uint32_t addr) {
    uint8_t chunk[PICO_SPIFFS_PROGRAM_PAGE];
    for (uint32_t offset = 0; offset < PICO_SPIFFS_ERASE_SIZE; offset += sizeof(chunk)) {
        if (pico_spiffs_flash_read(addr + offset, sizeof(chunk), chunk) != SPIFFS_OK)
            return false;
        for (size_t i = 0; i < sizeof(chunk); i++)
            if (chunk[i] != 0xFF)
                return false;
    }
    return true;
}

/* SPIFFS erases a whole block at a time. Erase only the sectors that are not
 * already blank, in runs, so a fresh chip formats quickly and a 64 KB-aligned
 * run can use the flash's block erase. */
static s32_t erase_skipping_blank(u32_t addr, u32_t size) {
    u32_t end = addr + size;
    while (addr < end) {
        while (addr < end && sector_blank(addr))
            addr += PICO_SPIFFS_ERASE_SIZE;
        u32_t run = addr;
        while (run < end && !sector_blank(run))
            run += PICO_SPIFFS_ERASE_SIZE;
        if (run > addr) {
            s32_t res = pico_spiffs_flash_erase(addr, run - addr);
            if (res != SPIFFS_OK)
                return res;
        }
        addr = run;
    }
    return SPIFFS_OK;
}

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

static s32_t configure(void) {
    if (!flash_ready) {
        s32_t res = pico_spiffs_flash_init();
        if (res != SPIFFS_OK)
            return res;
        flash_ready = true;
    }
    uint32_t room = pico_spiffs_flash_room(PICO_SPIFFS_FLASH_OFFSET);
    uint32_t size = PICO_SPIFFS_SIZE != 0 ? PICO_SPIFFS_SIZE : room;
    if (size == 0 || size > room)
        return SPIFFS_ERR_BAD_CONFIG;
    config = (spiffs_config){
        .hal_read_f = pico_spiffs_flash_read,
        .hal_write_f = pico_spiffs_flash_write,
        .hal_erase_f = erase_skipping_blank,
        .phys_size = size - size % PICO_SPIFFS_BLOCK_SIZE,
        .phys_addr = PICO_SPIFFS_FLASH_OFFSET,
        .phys_erase_block = PICO_SPIFFS_ERASE_SIZE,
        .log_block_size = PICO_SPIFFS_BLOCK_SIZE,
        .log_page_size = PICO_SPIFFS_PAGE_SIZE,
        .fh_ix_offset = PICO_SPIFFS_FIRST_FD - 1,
    };
    return SPIFFS_OK;
}

static s32_t mount_volume(void) {
    return SPIFFS_mount(&volume, &config, (u8_t *)work_buffer, (u8_t *)fd_buffer,
                        sizeof(fd_buffer), cache_buffer, sizeof(cache_buffer), NULL);
}

/* Erase and mount. Formatting needs the configuration from a mount
 * attempt (which fails on blank flash) and an unmounted volume. */
static s32_t format_and_mount(void) {
    if (!SPIFFS_mounted(&volume)) {
        /* settings SPIFFS refuses outright cannot be formatted either */
        s32_t res = mount_volume();
        if (res == SPIFFS_ERR_BAD_CONFIG || res == SPIFFS_ERR_MAGIC_NOT_POSSIBLE)
            return res;
    }
    SPIFFS_unmount(&volume);
    s32_t res = SPIFFS_format(&volume);
    if (res != SPIFFS_OK)
        return SPIFFS_errno(&volume) < 0 ? SPIFFS_errno(&volume) : res;
    return mount_volume();
}

/* True if the volume's flash looks erased: the first program page of every
 * erase sector is all 0xFF. A volume of any geometry, or other data, fails. */
static bool flash_blank(void) {
    uint8_t page[PICO_SPIFFS_PROGRAM_PAGE];
    for (uint32_t addr = config.phys_addr; addr < config.phys_addr + config.phys_size;
         addr += PICO_SPIFFS_ERASE_SIZE) {
        if (pico_spiffs_flash_read(addr, sizeof(page), page) != SPIFFS_OK)
            return false;
        for (size_t i = 0; i < sizeof(page); i++)
            if (page[i] != 0xFF)
                return false;
    }
    return true;
}

static bool should_format(pico_spiffs_mount_mode_t mode) {
    return mode == PICO_SPIFFS_FORMAT_IF_NEEDED ||
           (mode == PICO_SPIFFS_FORMAT_IF_BLANK && flash_blank());
}

static s32_t mount_locked(pico_spiffs_mount_mode_t mode) {
    if (SPIFFS_mounted(&volume))
        return SPIFFS_OK;
    s32_t res = configure();
    if (res != SPIFFS_OK)
        return res;
    res = mount_volume();
    if (res == SPIFFS_ERR_NOT_A_FS && should_format(mode))
        res = format_and_mount();
    return res;
}

static s32_t format_locked(void) {
    s32_t res = configure();
    if (res != SPIFFS_OK)
        return res;
    SPIFFS_unmount(&volume);
    return format_and_mount();
}

/* ------------------------------------------------------------------ */
/* Public functions                                                    */
/* ------------------------------------------------------------------ */

/* Mounting and formatting hold the volume lock throughout, so another core
 * cannot use the volume half-way through. */
int pico_spiffs_mount(pico_spiffs_mount_mode_t mode) {
    pico_spiffs_lock(&volume);
    s32_t res = mount_locked(mode);
    pico_spiffs_unlock(&volume);
    return res;
}

int pico_spiffs_format(void) {
    pico_spiffs_lock(&volume);
    s32_t res = format_locked();
    pico_spiffs_unlock(&volume);
    return res;
}

void pico_spiffs_unmount(void) {
    SPIFFS_unmount(&volume);        /* stores cached writes, closes files */
}

bool pico_spiffs_mounted(void) {
    return SPIFFS_mounted(&volume);
}

int pico_spiffs_info(uint32_t *total_bytes, uint32_t *used_bytes) {
    u32_t total = 0, used = 0;
    s32_t res = SPIFFS_info(&volume, &total, &used);
    if (total_bytes) *total_bytes = total;
    if (used_bytes) *used_bytes = used;
    return res;
}

int pico_spiffs_check(void) {
    return SPIFFS_check(&volume);
}

/* One fully deleted block erased per SPIFFS call, so the volume is locked
 * for one erase at a time. */
int pico_spiffs_gc(uint32_t max_blocks) {
    int erased = 0;
    while (max_blocks == 0 || (uint32_t)erased < max_blocks) {
        s32_t res = SPIFFS_gc_quick(&volume, 0);
        if (res == SPIFFS_ERR_NO_DELETED_BLOCKS)
            break;
        if (res != SPIFFS_OK)
            return res;
        erased++;
    }
    return erased;
}

spiffs *pico_spiffs_fs(void) {
    return &volume;
}
