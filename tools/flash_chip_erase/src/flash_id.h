/*
 * flash_id.h - decoding of SPI NOR flash identification and status bytes.
 *
 * Nothing here touches hardware, so it can be tested on a PC.
 */
#ifndef FLASH_ID_H
#define FLASH_ID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WINBOND_MANUFACTURER_ID 0xEF

#define JEDEC_ID_BYTES   3
#define UNIQUE_ID_BYTES  8
#define STATUS_REGISTERS 3

typedef enum {
    FLASH_KIND_NONE,       /* nothing answered */
    FLASH_KIND_UNKNOWN,    /* something answered, but not a Winbond flash */
    FLASH_KIND_WINBOND     /* a Winbond flash we can erase */
} flash_kind_t;

/* Identification read from one chip. */
typedef struct {
    uint8_t jedec[JEDEC_ID_BYTES];       /* 9Fh: manufacturer, type, capacity */
    uint8_t manufacturer_id;             /* 90h: manufacturer */
    uint8_t device_id;                   /* 90h: device */
    uint8_t unique_id[UNIQUE_ID_BYTES];  /* 4Bh (Winbond only) */
    uint8_t status[STATUS_REGISTERS];    /* 05h, 35h, 15h (Winbond only) */
} flash_info_t;

flash_kind_t flash_id_classify(const flash_info_t *info);

/* Capacity in bytes from the JEDEC capacity code, or 0 if unrecognised. */
uint32_t flash_id_capacity_bytes(const uint8_t jedec[JEDEC_ID_BYTES]);

/* Any block protection set, so that a chip erase would be ignored. */
bool flash_id_is_protected(const uint8_t status[STATUS_REGISTERS]);

/* Worst-case chip erase time from Winbond datasheets: 12.5 s per MB. */
uint32_t flash_id_erase_max_seconds(uint32_t capacity_bytes);

/* Typical chip erase time from Winbond datasheets: 2.5 s per MB. */
uint32_t flash_id_erase_typical_seconds(uint32_t capacity_bytes);

/* Bytes as lower-case hex separated by dashes, e.g. "ef-40-17". */
void flash_id_format(char *out, size_t out_size, const uint8_t *bytes, size_t count);

/* True if typed is the last 4 hex digits of the unique ID ("7788" or "77-88"). */
bool flash_id_matches_last4(const uint8_t unique_id[UNIQUE_ID_BYTES], const char *typed);

#endif
