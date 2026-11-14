/*
 * pico_spiffs_flash.h - what a flash backend provides to pico_spiffs.
 *
 * One backend is built: flash_onboard.c (the Pico's own QSPI flash) or
 * flash_external.c (a Winbond W25Q chip on SPI). Addresses are offsets
 * into that flash chip.
 */
#ifndef PICO_SPIFFS_FLASH_H
#define PICO_SPIFFS_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "spiffs.h"

/* NOR flash program page: SPIFFS writes are split so that no single
 * program operation crosses one. */
#define PICO_SPIFFS_PROGRAM_PAGE 256u

/* Erase unit both backends support. */
#define PICO_SPIFFS_ERASE_SIZE 4096u

/* Prepare the flash; returns SPIFFS_OK or an error. */
s32_t pico_spiffs_flash_init(void);

/* Bytes of flash the volume may use from flash_offset, or 0 if none. */
uint32_t pico_spiffs_flash_room(uint32_t flash_offset);

/* The SPIFFS HAL functions. */
s32_t pico_spiffs_flash_read(u32_t addr, u32_t size, u8_t *dst);
s32_t pico_spiffs_flash_write(u32_t addr, u32_t size, u8_t *src);
s32_t pico_spiffs_flash_erase(u32_t addr, u32_t size);

/* Activity LEDs (pico_spiffs_leds.c); no-ops unless pins are defined. */
void pico_spiffs_leds_init(void);
void pico_spiffs_led_read(bool on);
void pico_spiffs_led_write(bool on);
void pico_spiffs_led_erase(bool on);

#endif
