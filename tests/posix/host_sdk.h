/*
 * host_sdk.h - the simulated flash chips behind host_sdk.c.
 */
#ifndef HOST_SDK_H
#define HOST_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico.h"

#define W25Q_JEDEC_CAPACITY 0x17                    /* 8 MB, a W25Q64 */
#define W25Q_BYTES (1u << W25Q_JEDEC_CAPACITY)

typedef struct {
    uint32_t programs, erases, erases_64k, reads;
    uint64_t bytes_read;
    uint64_t bytes_programmed;
    uint32_t bit_set_attempts;      /* programming a 0 bit back to 1 */
    uint32_t rule_breaks;           /* SDK or W25Q rules broken */
} host_counts_t;

extern host_counts_t host_counts;
extern char host_stdout[4096];
extern size_t host_stdout_length;

/* Fill both chips with a byte (0xFF: erased) and clear the counts. */
void host_flash_reset(uint8_t fill);

/* The bytes of a chip from flash_offset. */
uint8_t *host_volume_bytes(uint32_t flash_offset, bool external);

#endif
