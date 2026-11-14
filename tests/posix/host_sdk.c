/*
 * host_sdk.c - what the Pico SDK and lib_winbond_w25q would provide, for
 * running pico_spiffs on a PC. Each stand-in checks the real function's
 * rules, so a backend that breaks them fails the tests:
 *
 *   flash_range_program  whole 256-byte pages at 256-byte aligned offsets
 *   flash_range_erase    4 KB aligned offsets and lengths
 *   W25Q page program    1..256 bytes, not crossing a 256-byte page
 *   W25Q erases          4 KB or 64 KB, aligned to their size
 *
 * Both chips are NOR flash: programming can only clear bits, and any
 * attempt to set one (which real flash would ignore) is counted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_sdk.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "winbond_W25Q_main.h"
#include "posix_stdio.h"

uint8_t host_flash[PICO_FLASH_SIZE_BYTES];
static uint8_t w25q_flash[W25Q_BYTES];

host_counts_t host_counts;

static void rule_broken(const char *what, uint32_t addr, size_t count) {
    fprintf(stderr, "SDK rule broken: %s at %08x, %zu bytes\n", what, addr, count);
    host_counts.rule_breaks++;
}

/* Programming a 1 bit leaves it unchanged, which is how the onboard backend
 * pads a partial page with 0xFF; so only bytes other than 0xFF that would
 * need a 0 bit set to 1 count as attempts to set bits. */
static void nor_program(uint8_t *flash, uint32_t addr, const uint8_t *data, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (data[i] != 0xFF && (flash[addr + i] & data[i]) != data[i])
            host_counts.bit_set_attempts++;
        flash[addr + i] &= data[i];
    }
    host_counts.programs++;
    host_counts.bytes_programmed += count;
}

void host_flash_reset(uint8_t fill) {
    memset(host_flash, fill, sizeof(host_flash));
    memset(w25q_flash, fill, sizeof(w25q_flash));
    memset(&host_counts, 0, sizeof(host_counts));
}

uint8_t *host_volume_bytes(uint32_t flash_offset, bool external) {
    return (external ? w25q_flash : host_flash) + flash_offset;
}

/* --- Onboard flash (hardware_flash, pico_flash) ------------------------- */

void flash_range_program(uint32_t flash_offs, const uint8_t *data, size_t count) {
    if (flash_offs % FLASH_PAGE_SIZE != 0 || count % FLASH_PAGE_SIZE != 0 || count == 0 ||
        flash_offs + count > PICO_FLASH_SIZE_BYTES) {
        rule_broken("flash_range_program", flash_offs, count);
        return;
    }
    nor_program(host_flash, flash_offs, data, count);
}

void flash_range_erase(uint32_t flash_offs, size_t count) {
    if (flash_offs % FLASH_SECTOR_SIZE != 0 || count % FLASH_SECTOR_SIZE != 0 ||
        flash_offs + count > PICO_FLASH_SIZE_BYTES) {
        rule_broken("flash_range_erase", flash_offs, count);
        return;
    }
    memset(host_flash + flash_offs, 0xFF, count);
    host_counts.erases++;
}

int flash_safe_execute(void (*func)(void *), void *param, uint32_t enter_exit_timeout_ms) {
    (void)enter_exit_timeout_ms;
    func(param);
    return PICO_OK;
}

/* --- External flash (lib_winbond_w25q) ---------------------------------- */

uint spi_get_baudrate(const struct spi_inst *spi) {
    (void)spi;
    return 37500000;
}

void lib_winbond_init_all(pico_spi_device_t *pico_spi) {
    (void)pico_spi;
}

int lib_winbond_query_jedec_id(pico_spi_device_t *pico_spi, uint8_t *query_result) {
    (void)pico_spi;
    query_result[0] = 0xEF;                 /* Winbond */
    query_result[1] = 0x40;
    query_result[2] = W25Q_JEDEC_CAPACITY;  /* log2 of the size */
    return 3;
}

uint32_t lib_winbond_program_flash_page(pico_spi_device_t *pico_spi, uint32_t address,
                                        const uint8_t *data, uint16_t length) {
    (void)pico_spi;
    if (length == 0 || length > 256 || address % 256 + length > 256 ||
        address + length > W25Q_BYTES) {
        rule_broken("W25Q page program", address, length);
        return 0;
    }
    nor_program(w25q_flash, address, data, length);
    return length;
}

static uint32_t w25q_erase(uint32_t address, uint32_t size) {
    if (address % size != 0 || address + size > W25Q_BYTES) {
        rule_broken("W25Q erase", address, size);
        return 0;
    }
    memset(w25q_flash + address, 0xFF, size);
    if (size == 65536)
        host_counts.erases_64k++;
    else
        host_counts.erases++;
    return size;
}

uint32_t lib_winbond_erase_4k_sector(pico_spi_device_t *pico_spi, uint32_t address) {
    (void)pico_spi;
    return w25q_erase(address, 4096);
}

uint32_t lib_winbond_erase_64k_block(pico_spi_device_t *pico_spi, uint32_t address) {
    (void)pico_spi;
    return w25q_erase(address, 65536);
}

static uint32_t w25q_read(uint32_t address, uint8_t *buffer, size_t length) {
    if (address + length > W25Q_BYTES) {
        rule_broken("W25Q read", address, length);
        return 0;
    }
    memcpy(buffer, w25q_flash + address, length);
    host_counts.reads++;
    host_counts.bytes_read += length;
    return (uint32_t)length;
}

int lib_winbond_read_data_spi(pico_spi_device_t *pico_spi, uint32_t address, uint8_t *buffer,
                              size_t length) {
    (void)pico_spi;
    return (int)w25q_read(address, buffer, length);
}

uint32_t lib_winbond_read_data_dma(pico_spi_device_t *pico_spi, uint32_t address,
                                   uint8_t *buffer, size_t length) {
    (void)pico_spi;
    return w25q_read(address, buffer, length);
}

/* --- Console (posix_stdio) ---------------------------------------------- */

char host_stdout[4096];
size_t host_stdout_length;

int posix_stdio_read(char *buffer, int length) {
    (void)buffer;
    (void)length;
    return 0;
}

int posix_stdio_write(const char *buffer, int length) {
    size_t room = sizeof(host_stdout) - host_stdout_length - 1;
    size_t n = (size_t)length < room ? (size_t)length : room;
    memcpy(host_stdout + host_stdout_length, buffer, n);
    host_stdout_length += n;
    host_stdout[host_stdout_length] = '\0';
    return length;
}
