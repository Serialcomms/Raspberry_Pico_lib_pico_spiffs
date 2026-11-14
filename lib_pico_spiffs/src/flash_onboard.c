/*
 * flash_onboard.c - pico_spiffs backend for the Pico's own QSPI flash.
 *
 * Reads go straight through the XIP window. Programs and erases use the
 * SDK's flash functions inside flash_safe_execute(), which keeps the other
 * core (and interrupts) away from flash while XIP is off. The SDK flushes
 * the XIP cache afterwards, so reads always see the new data.
 *
 * flash_range_program() only takes whole, aligned 256-byte pages. SPIFFS
 * writes a few bytes at a time, so each write is split at page boundaries
 * and padded with 0xFF: programming a 0xFF byte leaves NOR flash unchanged.
 */
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"

#include "pico_spiffs_flash.h"

/* How long flash_safe_execute() may wait for the other core to pause. */
#ifndef PICO_SPIFFS_SAFE_EXECUTE_TIMEOUT_MS
#define PICO_SPIFFS_SAFE_EXECUTE_TIMEOUT_MS 1000
#endif

extern char __flash_binary_end;     /* SDK linker symbol: end of the program */

typedef struct {
    uint32_t offset;
    const uint8_t *page;
} program_request_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
} erase_request_t;

uint32_t pico_spiffs_spi_baud(void) {
    return 0;                           /* QSPI flash, not an SPI port */
}

s32_t pico_spiffs_flash_init(void) {
    pico_spiffs_leds_init();
    return SPIFFS_OK;
}

/* The volume must lie after the program and inside the flash chip. */
uint32_t pico_spiffs_flash_room(uint32_t flash_offset) {
    uint32_t program_end = (uint32_t)((uintptr_t)&__flash_binary_end - XIP_BASE);
    if (flash_offset < program_end || flash_offset >= PICO_FLASH_SIZE_BYTES)
        return 0;
    return PICO_FLASH_SIZE_BYTES - flash_offset;
}

s32_t pico_spiffs_flash_read(u32_t addr, u32_t size, u8_t *dst) {
    pico_spiffs_led_read(true);
    memcpy(dst, (const void *)(XIP_BASE + addr), size);
    pico_spiffs_led_read(false);
    return SPIFFS_OK;
}

static void program_page_now(void *param) {
    const program_request_t *request = param;
    flash_range_program(request->offset, request->page, FLASH_PAGE_SIZE);
}

static void erase_now(void *param) {
    const erase_request_t *request = param;
    flash_range_erase(request->offset, request->size);
}

static s32_t safe_execute(void (*func)(void *), void *param) {
    int result = flash_safe_execute(func, param, PICO_SPIFFS_SAFE_EXECUTE_TIMEOUT_MS);
    return result == PICO_OK ? SPIFFS_OK : SPIFFS_ERR_INTERNAL;
}

/* Program len bytes at addr, all within one 256-byte flash page. */
static s32_t program_within_page(uint32_t addr, const uint8_t *src, uint32_t len) {
    uint8_t page[FLASH_PAGE_SIZE];
    uint32_t page_start = addr - (addr % FLASH_PAGE_SIZE);
    memset(page, 0xFF, sizeof(page));
    memcpy(page + (addr - page_start), src, len);
    program_request_t request = { page_start, page };
    return safe_execute(program_page_now, &request);
}

s32_t pico_spiffs_flash_write(u32_t addr, u32_t size, u8_t *src) {
    s32_t res = SPIFFS_OK;
    pico_spiffs_led_write(true);
    while (size > 0 && res == SPIFFS_OK) {
        uint32_t room = FLASH_PAGE_SIZE - (addr % FLASH_PAGE_SIZE);
        uint32_t len = size < room ? size : room;
        res = program_within_page(addr, src, len);
        addr += len;
        src += len;
        size -= len;
    }
    pico_spiffs_led_write(false);
    return res;
}

s32_t pico_spiffs_flash_erase(u32_t addr, u32_t size) {
    pico_spiffs_led_erase(true);
    erase_request_t request = { addr, size };
    s32_t res = safe_execute(erase_now, &request);
    pico_spiffs_led_erase(false);
    return res;
}
