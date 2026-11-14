/*
 * flash_external.c - pico_spiffs backend for a Winbond W25Q chip on SPI,
 * using lib_winbond_w25q.
 *
 * The chip's size comes from its JEDEC ID. Writes are split at 256-byte
 * page boundaries, as a W25Q page program cannot cross one. Erases use
 * 64 KB block erases where they can and 4 KB sector erases otherwise.
 */
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "winbond_W25Q_main.h"

#include "pico_spiffs_flash.h"

/* Wiring: defaults match the Pico-spiffs and lib_winbond_w25q examples. */
#ifndef PICO_SPIFFS_EXT_SPI
#define PICO_SPIFFS_EXT_SPI spi0
#endif
#ifndef PICO_SPIFFS_EXT_PIN_MISO
#define PICO_SPIFFS_EXT_PIN_MISO 16
#endif
#ifndef PICO_SPIFFS_EXT_PIN_CS
#define PICO_SPIFFS_EXT_PIN_CS 17
#endif
#ifndef PICO_SPIFFS_EXT_PIN_SCK
#define PICO_SPIFFS_EXT_PIN_SCK 18
#endif
#ifndef PICO_SPIFFS_EXT_PIN_MOSI
#define PICO_SPIFFS_EXT_PIN_MOSI 19
#endif
#ifndef PICO_SPIFFS_EXT_BAUD
#define PICO_SPIFFS_EXT_BAUD (40 * 1000 * 1000)
#endif

/* Reads at least this long use DMA; shorter ones are not worth setting up. */
#define DMA_READ_MIN 64u

#define BLOCK_64K (64u * 1024u)

static pico_spi_device_t winbond = {
    .PIN_CS = PICO_SPIFFS_EXT_PIN_CS,
    .PIN_SCK = PICO_SPIFFS_EXT_PIN_SCK,
    .PIN_MOSI = PICO_SPIFFS_EXT_PIN_MOSI,
    .PIN_MISO = PICO_SPIFFS_EXT_PIN_MISO,
    .SPI_BAUD = PICO_SPIFFS_EXT_BAUD,
    .DMA_BYTES = PICO_SPIFFS_PROGRAM_PAGE,
    .device = PICO_SPIFFS_EXT_SPI,
};

static uint32_t chip_size;      /* bytes, from the JEDEC ID; 0 until known */

/* JEDEC ID byte 2 is log2 of the capacity: 0x17 for an 8 MB W25Q64. */
static uint32_t size_from_jedec(const uint8_t jedec[3]) {
    uint8_t capacity = jedec[2];
    if (capacity < 0x10 || capacity > 0x19)     /* 64 KB .. 32 MB: 24-bit addressing */
        return 0;
    return 1u << capacity;
}

/* The clock the SPI divider actually gives, which may be below the request. */
uint32_t pico_spiffs_spi_baud(void) {
    return chip_size != 0 ? spi_get_baudrate(winbond.device) : 0;
}

s32_t pico_spiffs_flash_init(void) {
    if (chip_size != 0)
        return SPIFFS_OK;
    pico_spiffs_leds_init();
    lib_winbond_init_all(&winbond);
    uint8_t jedec[3] = { 0 };
    lib_winbond_query_jedec_id(&winbond, jedec);
    chip_size = size_from_jedec(jedec);
    return chip_size != 0 ? SPIFFS_OK : SPIFFS_ERR_NOT_CONFIGURED;
}

uint32_t pico_spiffs_flash_room(uint32_t flash_offset) {
    return flash_offset < chip_size ? chip_size - flash_offset : 0;
}

s32_t pico_spiffs_flash_read(u32_t addr, u32_t size, u8_t *dst) {
    pico_spiffs_led_read(true);
    uint32_t got = size >= DMA_READ_MIN
                       ? lib_winbond_read_data_dma(&winbond, addr, dst, size)
                       : (uint32_t)lib_winbond_read_data_spi(&winbond, addr, dst, size);
    pico_spiffs_led_read(false);
    return got == size ? SPIFFS_OK : SPIFFS_ERR_INTERNAL;
}

s32_t pico_spiffs_flash_write(u32_t addr, u32_t size, u8_t *src) {
    s32_t res = SPIFFS_OK;
    pico_spiffs_led_write(true);
    while (size > 0 && res == SPIFFS_OK) {
        uint32_t room = PICO_SPIFFS_PROGRAM_PAGE - (addr % PICO_SPIFFS_PROGRAM_PAGE);
        uint32_t len = size < room ? size : room;
        uint32_t done = lib_winbond_program_flash_page(&winbond, addr, src, (uint16_t)len);
        res = done == len ? SPIFFS_OK : SPIFFS_ERR_INTERNAL;
        addr += len;
        src += len;
        size -= len;
    }
    pico_spiffs_led_write(false);
    return res;
}

/* Erase the largest unit that starts at addr and fits in size. */
static uint32_t erase_one(uint32_t addr, uint32_t size) {
    if (addr % BLOCK_64K == 0 && size >= BLOCK_64K)
        return lib_winbond_erase_64k_block(&winbond, addr) == BLOCK_64K ? BLOCK_64K : 0;
    return lib_winbond_erase_4k_sector(&winbond, addr) == PICO_SPIFFS_ERASE_SIZE
               ? PICO_SPIFFS_ERASE_SIZE : 0;
}

s32_t pico_spiffs_flash_erase(u32_t addr, u32_t size) {
    s32_t res = SPIFFS_OK;
    pico_spiffs_led_erase(true);
    while (size > 0 && res == SPIFFS_OK) {
        uint32_t erased = erase_one(addr, size);
        res = erased != 0 ? SPIFFS_OK : SPIFFS_ERR_ERASE_FAIL;
        addr += erased;
        size -= erased;
    }
    pico_spiffs_led_erase(false);
    return res;
}
