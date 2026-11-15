/*
 * flash_id.c - decoding of SPI NOR flash identification and status bytes.
 */
#include <ctype.h>
#include <stdio.h>

#include "flash_id.h"

/* Status register bits that stop a chip erase (Winbond W25Q datasheets). */
#define SR1_BLOCK_PROTECT_BITS 0x7C   /* BP0-BP2 (and BP3 on 256 Mbit+), TB, SEC */
#define SR2_COMPLEMENT_BIT     0x40   /* CMP: with BP=0, protects everything */
#define SR3_WPS_BIT            0x04   /* individual block locks in use */

static bool all_bytes_equal(const uint8_t *bytes, size_t count, uint8_t value) {
    for (size_t i = 0; i < count; i++) {
        if (bytes[i] != value)
            return false;
    }
    return true;
}

static bool nothing_answered(const flash_info_t *info) {
    return all_bytes_equal(info->jedec, JEDEC_ID_BYTES, 0x00) ||
           all_bytes_equal(info->jedec, JEDEC_ID_BYTES, 0xFF);
}

flash_kind_t flash_id_classify(const flash_info_t *info) {
    if (nothing_answered(info))
        return FLASH_KIND_NONE;
    bool winbond = info->jedec[0] == WINBOND_MANUFACTURER_ID &&
                   info->manufacturer_id == WINBOND_MANUFACTURER_ID &&
                   flash_id_capacity_bytes(info->jedec) != 0;
    return winbond ? FLASH_KIND_WINBOND : FLASH_KIND_UNKNOWN;
}

/* Codes 11h-19h are 2^code bytes; JEDEC then continues at 20h (512 Mbit). */
uint32_t flash_id_capacity_bytes(const uint8_t jedec[JEDEC_ID_BYTES]) {
    uint8_t code = jedec[2];
    if (code >= 0x11 && code <= 0x19)
        return 1u << code;
    if (code == 0x20 || code == 0x21)
        return 1u << (code - 0x20 + 26);
    return 0;
}

bool flash_id_is_protected(const uint8_t status[STATUS_REGISTERS]) {
    return (status[0] & SR1_BLOCK_PROTECT_BITS) != 0 ||
           (status[1] & SR2_COMPLEMENT_BIT) != 0 ||
           (status[2] & SR3_WPS_BIT) != 0;
}

uint32_t flash_id_erase_max_seconds(uint32_t capacity_bytes) {
    uint32_t megabytes = capacity_bytes / (1024u * 1024u);
    uint32_t seconds = (megabytes * 25u + 1u) / 2u;
    return seconds < 10u ? 10u : seconds;
}

uint32_t flash_id_erase_typical_seconds(uint32_t capacity_bytes) {
    uint32_t megabytes = capacity_bytes / (1024u * 1024u);
    uint32_t seconds = (megabytes * 5u + 1u) / 2u;
    return seconds < 2u ? 2u : seconds;
}

void flash_id_format(char *out, size_t out_size, const uint8_t *bytes, size_t count) {
    size_t used = 0;
    if (out_size == 0)
        return;
    out[0] = '\0';
    for (size_t i = 0; i < count && used + 3 < out_size; i++) {
        int n = snprintf(out + used, out_size - used, i == 0 ? "%02x" : "-%02x", bytes[i]);
        if (n < 0)
            break;
        used += (size_t)n;
    }
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/* Collect hex digits, skipping dashes and spaces; -1 on anything else. */
static int typed_hex_digits(const char *typed, int digits[], int max_digits) {
    int count = 0;
    for (; *typed != '\0'; typed++) {
        if (*typed == '-' || *typed == ' ')
            continue;
        int value = hex_value(*typed);
        if (value < 0 || count == max_digits)
            return -1;
        digits[count++] = value;
    }
    return count;
}

bool flash_id_matches_last4(const uint8_t unique_id[UNIQUE_ID_BYTES], const char *typed) {
    int digits[4];
    if (typed_hex_digits(typed, digits, 4) != 4)
        return false;
    uint8_t high = unique_id[UNIQUE_ID_BYTES - 2];
    uint8_t low = unique_id[UNIQUE_ID_BYTES - 1];
    return digits[0] == high >> 4 && digits[1] == (high & 0x0F) &&
           digits[2] == low >> 4 && digits[3] == (low & 0x0F);
}
