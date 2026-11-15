/*
 * host_test.c - PC tests of the hardware-independent parts: ID decoding,
 * protection check, erase time, formatting, and menu input parsing.
 *
 *   cc -std=c11 -Wall -Wextra -I../src host_test.c ../src/flash_id.c \
 *      ../src/text_input.c -o host_test && ./host_test
 */
#include <stdio.h>
#include <string.h>

#include "flash_id.h"
#include "text_input.h"

static int checks;
static int failures;

static void check(int ok, const char *what) {
    checks++;
    if (!ok) {
        failures++;
        printf("FAILED: %s\n", what);
    }
}

static flash_info_t info_with(uint8_t j0, uint8_t j1, uint8_t j2, uint8_t mfr) {
    flash_info_t info;
    memset(&info, 0, sizeof(info));
    info.jedec[0] = j0;
    info.jedec[1] = j1;
    info.jedec[2] = j2;
    info.manufacturer_id = mfr;
    return info;
}

static void test_classify(void) {
    flash_info_t w25q64 = info_with(0xEF, 0x40, 0x17, 0xEF);
    flash_info_t empty_ff = info_with(0xFF, 0xFF, 0xFF, 0xFF);
    flash_info_t empty_00 = info_with(0x00, 0x00, 0x00, 0x00);
    flash_info_t psram = info_with(0x0D, 0x5D, 0x52, 0x00);
    flash_info_t gigadevice = info_with(0xC8, 0x40, 0x17, 0xC8);
    flash_info_t odd_capacity = info_with(0xEF, 0x40, 0x05, 0xEF);
    flash_info_t mfr_mismatch = info_with(0xEF, 0x40, 0x17, 0xFF);
    check(flash_id_classify(&w25q64) == FLASH_KIND_WINBOND, "W25Q64 is Winbond");
    check(flash_id_classify(&empty_ff) == FLASH_KIND_NONE, "all FFh is nothing");
    check(flash_id_classify(&empty_00) == FLASH_KIND_NONE, "all 00h is nothing");
    check(flash_id_classify(&psram) == FLASH_KIND_UNKNOWN, "PSRAM-like ID is unknown");
    check(flash_id_classify(&gigadevice) == FLASH_KIND_UNKNOWN, "GigaDevice is not erasable");
    check(flash_id_classify(&odd_capacity) == FLASH_KIND_UNKNOWN, "bad capacity code rejected");
    check(flash_id_classify(&mfr_mismatch) == FLASH_KIND_UNKNOWN, "90h must agree with 9Fh");
}

static void test_capacity(void) {
    const uint8_t q16[3] = { 0xEF, 0x40, 0x15 };
    const uint8_t q64[3] = { 0xEF, 0x40, 0x17 };
    const uint8_t q128[3] = { 0xEF, 0x40, 0x18 };
    const uint8_t q256[3] = { 0xEF, 0x40, 0x19 };
    const uint8_t q512[3] = { 0xEF, 0x40, 0x20 };
    const uint8_t q01[3] = { 0xEF, 0x40, 0x21 };
    const uint8_t bad[3] = { 0xEF, 0x40, 0x1A };
    check(flash_id_capacity_bytes(q16) == 2u * 1024 * 1024, "W25Q16 = 2 MB");
    check(flash_id_capacity_bytes(q64) == 8u * 1024 * 1024, "W25Q64 = 8 MB");
    check(flash_id_capacity_bytes(q128) == 16u * 1024 * 1024, "W25Q128 = 16 MB");
    check(flash_id_capacity_bytes(q256) == 32u * 1024 * 1024, "W25Q256 = 32 MB");
    check(flash_id_capacity_bytes(q512) == 64u * 1024 * 1024, "W25Q512 = 64 MB");
    check(flash_id_capacity_bytes(q01) == 128u * 1024 * 1024, "W25Q01 = 128 MB");
    check(flash_id_capacity_bytes(bad) == 0, "1Ah is not a capacity code");
}

static void test_protection(void) {
    const uint8_t clear[3] = { 0x00, 0x02, 0x60 };     /* QE set, drive strength */
    const uint8_t busy_wel[3] = { 0x03, 0x00, 0x00 };   /* BUSY and WEL only */
    const uint8_t bp0[3] = { 0x04, 0x00, 0x00 };
    const uint8_t tb[3] = { 0x20, 0x00, 0x00 };
    const uint8_t cmp[3] = { 0x00, 0x40, 0x00 };
    const uint8_t wps[3] = { 0x00, 0x00, 0x04 };
    const uint8_t srp[3] = { 0x80, 0x00, 0x00 };        /* SRP alone protects no data */
    check(!flash_id_is_protected(clear), "QE and drive strength are not protection");
    check(!flash_id_is_protected(busy_wel), "BUSY/WEL are not protection");
    check(!flash_id_is_protected(srp), "SRP alone is not block protection");
    check(flash_id_is_protected(bp0), "BP0 protects");
    check(flash_id_is_protected(tb), "TB/BP3 bit counted");
    check(flash_id_is_protected(cmp), "CMP protects");
    check(flash_id_is_protected(wps), "WPS counted");
}

static void test_erase_time(void) {
    check(flash_id_erase_max_seconds(4u * 1024 * 1024) == 50, "W25Q32: 50 s");
    check(flash_id_erase_max_seconds(8u * 1024 * 1024) == 100, "W25Q64: 100 s");
    check(flash_id_erase_max_seconds(16u * 1024 * 1024) == 200, "W25Q128: 200 s");
    check(flash_id_erase_max_seconds(32u * 1024 * 1024) == 400, "W25Q256: 400 s");
    check(flash_id_erase_max_seconds(512u * 1024) == 10, "small chip: 10 s minimum");
    check(flash_id_erase_typical_seconds(4u * 1024 * 1024) == 10, "W25Q32 typical: 10 s");
    check(flash_id_erase_typical_seconds(8u * 1024 * 1024) == 20, "W25Q64 typical: 20 s");
    check(flash_id_erase_typical_seconds(16u * 1024 * 1024) == 40, "W25Q128 typical: 40 s");
    check(flash_id_erase_typical_seconds(32u * 1024 * 1024) == 80, "W25Q256 typical: 80 s");
    check(flash_id_erase_typical_seconds(256u * 1024) == 2, "small chip: 2 s minimum");
}

static void test_format(void) {
    char text[32];
    const uint8_t jedec[3] = { 0xEF, 0x40, 0x17 };
    const uint8_t unique[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    flash_id_format(text, sizeof(text), jedec, 3);
    check(strcmp(text, "ef-40-17") == 0, "JEDEC formats as ef-40-17");
    flash_id_format(text, sizeof(text), unique, 8);
    check(strcmp(text, "11-22-33-44-55-66-77-88") == 0, "unique ID formats with dashes");
    flash_id_format(text, 6, unique, 8);
    check(strcmp(text, "11-22") == 0, "format stops cleanly when out of room");
}

static void test_last4(void) {
    const uint8_t unique[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xA7, 0x8F };
    check(flash_id_matches_last4(unique, "a78f"), "lower case matches");
    check(flash_id_matches_last4(unique, "A78F"), "upper case matches");
    check(flash_id_matches_last4(unique, "a7-8f"), "dash allowed");
    check(flash_id_matches_last4(unique, " a7 8f "), "spaces allowed");
    check(!flash_id_matches_last4(unique, "a78e"), "wrong digit rejected");
    check(!flash_id_matches_last4(unique, "78f"), "too few digits rejected");
    check(!flash_id_matches_last4(unique, "6a78f"), "too many digits rejected");
    check(!flash_id_matches_last4(unique, "8fa7"), "swapped bytes rejected");
    check(!flash_id_matches_last4(unique, ""), "blank rejected");
    check(!flash_id_matches_last4(unique, "a7x8f"), "non-hex rejected");
}

static void test_text_input(void) {
    check(text_is("s", "S"), "s means S");
    check(text_is(" S ", "S"), "spaces around S ignored");
    check(text_is("boot", "BOOT"), "boot means BOOT");
    check(!text_is("bootx", "BOOT"), "bootx is not BOOT");
    check(!text_is("", "S"), "blank is not S");
    check(text_option_index("A", 1) == 0, "A is option 0");
    check(text_option_index("b", 2) == 1, "b is option 1");
    check(text_option_index("B", 1) == -1, "B is out of range with 1 option");
    check(text_option_index("AB", 2) == -1, "two letters rejected");
    check(text_option_index("1", 4) == -1, "digit rejected");
    check(text_option_index("", 4) == -1, "blank rejected");
    check(text_is_blank("   "), "spaces are blank");
    check(!text_is_blank(" 7"), "digit is not blank");
}

int main(void) {
    test_classify();
    test_capacity();
    test_protection();
    test_erase_time();
    test_format();
    test_last4();
    test_text_input();
    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
