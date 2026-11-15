/*
 * flash_chip.h - standard SPI NOR commands sent through a flash_port_t.
 */
#ifndef FLASH_CHIP_H
#define FLASH_CHIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_id.h"
#include "flash_port.h"

/* JEDEC and device IDs, plus unique ID and status registers for Winbond. */
flash_kind_t flash_chip_identify(const flash_port_t *port, flash_info_t *info);

uint8_t flash_chip_read_status(const flash_port_t *port, unsigned int reg);

bool flash_chip_is_busy(const flash_port_t *port);

/* Sends Write Enable (06h); true if the chip then reports WEL set. */
bool flash_chip_write_enable(const flash_port_t *port);

/* Sends Chip Erase (60h). The chip stays busy until the erase finishes. */
void flash_chip_start_erase(const flash_port_t *port);

/* Reads up to FLASH_CHIP_MAX_READ bytes; 4-byte addresses above 16 MB. */
#define FLASH_CHIP_MAX_READ 256
void flash_chip_read(const flash_port_t *port, uint32_t address, uint8_t *data, size_t count);

#endif
