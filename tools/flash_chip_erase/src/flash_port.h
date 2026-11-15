/*
 * flash_port.h - the four places a flash chip can be connected: QSPI chip
 * selects 0 and 1, and the SPI0 and SPI1 peripherals.
 *
 * Each transfer asserts the chip select, clocks count bytes out of tx while
 * reading count bytes into rx, then releases the chip select.
 */
#ifndef FLASH_PORT_H
#define FLASH_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLASH_PORT_COUNT 4

typedef enum {
    PORT_QSPI,
    PORT_SPI
} flash_port_kind_t;

typedef struct {
    const char *name;           /* e.g. "QSPI channel 0" */
    flash_port_kind_t kind;
    unsigned int channel;       /* QSPI chip select, or SPI instance */
    int pin_cs;                 /* SPI chip select, or QSPI CS1 GPIO; -1 if none */
    int pin_sck, pin_mosi, pin_miso;  /* SPI only */
    bool available;             /* wired up on this build */
} flash_port_t;

void flash_ports_init(void);

flash_port_t *flash_port_get(size_t index);

void flash_port_transfer(const flash_port_t *port, const uint8_t *tx, uint8_t *rx, size_t count);

#endif
