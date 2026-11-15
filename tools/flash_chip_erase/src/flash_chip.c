/*
 * flash_chip.c - standard SPI NOR commands sent through a flash_port_t.
 *
 * Identification only uses read commands, so probing a channel with
 * something else on it (such as PSRAM on QSPI CS1) changes nothing.
 */
#include <string.h>

#include "flash_chip.h"

#define CMD_WRITE_ENABLE    0x06
#define CMD_READ_STATUS_1   0x05
#define CMD_READ_STATUS_2   0x35
#define CMD_READ_STATUS_3   0x15
#define CMD_CHIP_ERASE      0x60
#define CMD_READ_DATA       0x03
#define CMD_READ_DATA_4B    0x13
#define CMD_DEVICE_ID       0x90
#define CMD_JEDEC_ID        0x9F
#define CMD_UNIQUE_ID       0x4B

#define STATUS_BUSY_BIT 0x01
#define STATUS_WEL_BIT  0x02

#define UNIQUE_ID_DUMMY_BYTES 4
#define THREE_BYTE_ADDRESS_LIMIT (16u * 1024u * 1024u)

/* Sends cmd followed by count-1 zero bytes; reply bytes land in rx. */
static void command(const flash_port_t *port, uint8_t cmd, uint8_t *rx, size_t count) {
    uint8_t tx[1 + UNIQUE_ID_DUMMY_BYTES + UNIQUE_ID_BYTES] = { 0 };
    tx[0] = cmd;
    flash_port_transfer(port, tx, rx, count);
}

static void read_jedec_id(const flash_port_t *port, flash_info_t *info) {
    uint8_t rx[1 + JEDEC_ID_BYTES];
    command(port, CMD_JEDEC_ID, rx, sizeof(rx));
    memcpy(info->jedec, &rx[1], JEDEC_ID_BYTES);
}

/* 90h is followed by a 24-bit address of 0, then manufacturer and device. */
static void read_device_id(const flash_port_t *port, flash_info_t *info) {
    uint8_t rx[1 + 3 + 2];
    command(port, CMD_DEVICE_ID, rx, sizeof(rx));
    info->manufacturer_id = rx[4];
    info->device_id = rx[5];
}

static void read_unique_id(const flash_port_t *port, flash_info_t *info) {
    uint8_t rx[1 + UNIQUE_ID_DUMMY_BYTES + UNIQUE_ID_BYTES];
    command(port, CMD_UNIQUE_ID, rx, sizeof(rx));
    memcpy(info->unique_id, &rx[1 + UNIQUE_ID_DUMMY_BYTES], UNIQUE_ID_BYTES);
}

static void read_all_status(const flash_port_t *port, flash_info_t *info) {
    for (unsigned int reg = 1; reg <= STATUS_REGISTERS; reg++)
        info->status[reg - 1] = flash_chip_read_status(port, reg);
}

flash_kind_t flash_chip_identify(const flash_port_t *port, flash_info_t *info) {
    memset(info, 0, sizeof(*info));
    read_jedec_id(port, info);
    read_device_id(port, info);
    flash_kind_t kind = flash_id_classify(info);
    if (kind == FLASH_KIND_WINBOND) {
        read_unique_id(port, info);
        read_all_status(port, info);
    }
    return kind;
}

uint8_t flash_chip_read_status(const flash_port_t *port, unsigned int reg) {
    static const uint8_t commands[STATUS_REGISTERS] = {
        CMD_READ_STATUS_1, CMD_READ_STATUS_2, CMD_READ_STATUS_3
    };
    uint8_t rx[2];
    command(port, commands[reg - 1], rx, sizeof(rx));
    return rx[1];
}

bool flash_chip_is_busy(const flash_port_t *port) {
    return (flash_chip_read_status(port, 1) & STATUS_BUSY_BIT) != 0;
}

bool flash_chip_write_enable(const flash_port_t *port) {
    uint8_t rx[1];
    command(port, CMD_WRITE_ENABLE, rx, sizeof(rx));
    return (flash_chip_read_status(port, 1) & STATUS_WEL_BIT) != 0;
}

void flash_chip_start_erase(const flash_port_t *port) {
    uint8_t rx[1];
    command(port, CMD_CHIP_ERASE, rx, sizeof(rx));
}

static size_t put_read_command(uint8_t *tx, uint32_t address) {
    size_t n = 0;
    if (address >= THREE_BYTE_ADDRESS_LIMIT) {
        tx[n++] = CMD_READ_DATA_4B;
        tx[n++] = (uint8_t)(address >> 24);
    } else {
        tx[n++] = CMD_READ_DATA;
    }
    tx[n++] = (uint8_t)(address >> 16);
    tx[n++] = (uint8_t)(address >> 8);
    tx[n++] = (uint8_t)address;
    return n;
}

void flash_chip_read(const flash_port_t *port, uint32_t address, uint8_t *data, size_t count) {
    uint8_t tx[5 + FLASH_CHIP_MAX_READ] = { 0 };
    uint8_t rx[5 + FLASH_CHIP_MAX_READ];
    if (count > FLASH_CHIP_MAX_READ)
        count = FLASH_CHIP_MAX_READ;
    size_t header = put_read_command(tx, address);
    flash_port_transfer(port, tx, rx, header + count);
    memcpy(data, &rx[header], count);
}
