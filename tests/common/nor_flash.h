/*
 * nor_flash.h - a NOR flash chip in RAM, for host tests.
 *
 * Programming can only clear bits; erasing sets a whole erase block to
 * 0xFF. Every access is checked against the chip's bounds and the erase
 * block size, and an attempt to set a bit by programming is reported, as
 * real flash would silently keep the 0.
 */
#ifndef NOR_FLASH_H
#define NOR_FLASH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t *bytes;
    uint32_t size;
    uint32_t erase_size;
    uint32_t reads, programs, erases;           /* operation counts */
    uint64_t bytes_read, bytes_programmed;
    uint32_t bit_set_attempts;                  /* programming 0 -> 1 */
    uint32_t ops_until_power_cut;               /* 0: never */
    bool power_cut;                             /* writes now ignored */
} nor_flash_t;

void nor_flash_init(nor_flash_t *flash, uint32_t size, uint32_t erase_size);
void nor_flash_free(nor_flash_t *flash);
void nor_flash_reset_counts(nor_flash_t *flash);

/* Return 0, or -1 if the access is out of bounds or misaligned. */
int nor_flash_read(nor_flash_t *flash, uint32_t addr, uint32_t size, uint8_t *dst);
int nor_flash_program(nor_flash_t *flash, uint32_t addr, uint32_t size,
                      const uint8_t *src);
int nor_flash_erase(nor_flash_t *flash, uint32_t addr, uint32_t size);

#endif
