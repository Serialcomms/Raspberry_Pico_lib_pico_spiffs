/*
 * flash_erase.h - full chip erase with progress dots, LED pulse and a
 * blank check afterwards.
 */
#ifndef FLASH_ERASE_H
#define FLASH_ERASE_H

#include <stdbool.h>

#include "flash_id.h"
#include "flash_port.h"

/* Returns true if the erase completed and the chip reads back blank. */
bool flash_erase_chip(const flash_port_t *port, const flash_info_t *info);

#endif
