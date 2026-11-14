/*
 * nor_flash.c - a NOR flash chip in RAM, for host tests.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nor_flash.h"

#ifdef NOR_FLASH_TRACE_BIT_SET
#include <sanitizer/common_interface_defs.h>
#endif

void nor_flash_init(nor_flash_t *flash, uint32_t size, uint32_t erase_size) {
    memset(flash, 0, sizeof(*flash));
    flash->bytes = malloc(size);
    flash->size = size;
    flash->erase_size = erase_size;
    memset(flash->bytes, 0xFF, size);
}

void nor_flash_free(nor_flash_t *flash) {
    free(flash->bytes);
    flash->bytes = NULL;
}

void nor_flash_reset_counts(nor_flash_t *flash) {
    flash->reads = flash->programs = flash->erases = 0;
    flash->bytes_read = flash->bytes_programmed = 0;
}

static bool in_bounds(const nor_flash_t *flash, uint32_t addr, uint32_t size) {
    return addr <= flash->size && size <= flash->size - addr;
}

/* Count a write operation; once the power-cut count is reached, every
 * later write is lost, as if power failed at that moment. */
static bool power_still_on(nor_flash_t *flash) {
    if (flash->ops_until_power_cut == 0)
        return !flash->power_cut;
    if (--flash->ops_until_power_cut == 0)
        flash->power_cut = true;
    return !flash->power_cut;
}

int nor_flash_read(nor_flash_t *flash, uint32_t addr, uint32_t size, uint8_t *dst) {
    if (!in_bounds(flash, addr, size)) {
        fprintf(stderr, "nor_flash: read out of bounds %08x+%u\n", addr, size);
        return -1;
    }
    memcpy(dst, flash->bytes + addr, size);
    flash->reads++;
    flash->bytes_read += size;
    return 0;
}

int nor_flash_program(nor_flash_t *flash, uint32_t addr, uint32_t size,
                      const uint8_t *src) {
    if (!in_bounds(flash, addr, size)) {
        fprintf(stderr, "nor_flash: program out of bounds %08x+%u\n", addr, size);
        return -1;
    }
    flash->programs++;
    flash->bytes_programmed += size;
    if (!power_still_on(flash))
        return 0;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t old = flash->bytes[addr + i];
        if ((old & src[i]) != src[i]) {
#ifdef NOR_FLASH_TRACE_BIT_SET
            if (flash->bit_set_attempts == 0) {
                fprintf(stderr, "nor_flash: bit set at %08x (+%u of %08x+%u): %02x -> %02x\n",
                        addr + i, i, addr, size, old, src[i]);
                __sanitizer_print_stack_trace();
            }
#endif
            flash->bit_set_attempts++;
        }
        flash->bytes[addr + i] = old & src[i];
    }
    return 0;
}

int nor_flash_erase(nor_flash_t *flash, uint32_t addr, uint32_t size) {
    if (!in_bounds(flash, addr, size) || addr % flash->erase_size != 0 ||
        size % flash->erase_size != 0) {
        fprintf(stderr, "nor_flash: bad erase %08x+%u\n", addr, size);
        return -1;
    }
    flash->erases++;
    if (!power_still_on(flash))
        return 0;
    memset(flash->bytes + addr, 0xFF, size);
    return 0;
}
