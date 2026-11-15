/*
 * flash_chip_erase - finds flash on QSPI chip selects 0 and 1 and on SPI0
 * and SPI1, then performs a full chip erase on the one the user picks.
 *
 * Runs from RAM (a no_flash binary), so loading it writes nothing to flash
 * and the onboard flash on QSPI channel 0 can be erased too.
 */
#include <stdio.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "console.h"
#include "flash_chip.h"
#include "flash_erase.h"
#include "flash_id.h"
#include "flash_port.h"
#include "led_pulse.h"
#include "text_input.h"

#ifndef FLASH_ERASE_VERSION
#define FLASH_ERASE_VERSION "?"
#endif

#define LINE_SIZE 32
#define ID_TEXT_SIZE (UNIQUE_ID_BYTES * 3)
#define SEPARATOR "==========================================="

typedef struct {
    const flash_port_t *port;
    flash_info_t info;
} erasable_flash_t;

/* ------------------------------------------------------------------ */
/* Scan results                                                        */
/* ------------------------------------------------------------------ */

static void print_id(const char *label, const uint8_t *bytes, size_t count) {
    char text[ID_TEXT_SIZE];
    flash_id_format(text, sizeof(text), bytes, count);
    printf("%s = %s\n", label, text);
}

static void print_size(uint32_t bytes) {
    const uint32_t megabyte = 1024u * 1024u;
    if (bytes >= megabyte)
        printf("Flash size        = %lu MB\n", (unsigned long)(bytes / megabyte));
    else
        printf("Flash size        = %lu KB\n", (unsigned long)(bytes / 1024u));
}

static void print_winbond_ids(const flash_info_t *info) {
    uint8_t device[2] = { info->manufacturer_id, info->device_id };
    print_id("Winbond Device ID", device, sizeof(device));
    print_id("Winbond JEDEC  ID", info->jedec, JEDEC_ID_BYTES);
    print_id("Winbond Unique ID", info->unique_id, UNIQUE_ID_BYTES);
    print_size(flash_id_capacity_bytes(info->jedec));
}

static void print_erasable(const char *name, const flash_info_t *info, char option) {
    printf("\n" SEPARATOR "\n");
    printf("Erasable flash detected on %s\n\n", name);
    print_winbond_ids(info);
    printf("\nSelect option %c to perform full chip erase\n", option);
}

static void print_protected(const char *name, const flash_info_t *info) {
    printf("\n" SEPARATOR "\n");
    printf("Write-protected flash detected on %s (not erasable)\n\n", name);
    print_winbond_ids(info);
    print_id("Status registers ", info->status, STATUS_REGISTERS);
}

static void print_unknown(const char *name, const flash_info_t *info) {
    char text[ID_TEXT_SIZE];
    flash_id_format(text, sizeof(text), info->jedec, JEDEC_ID_BYTES);
    printf("\nUnrecognised device on %s (JEDEC ID = %s): not erasable\n", name, text);
}

/* Adds an erasable chip to the list and prints it; returns the new count. */
static size_t report_port(const flash_port_t *port, erasable_flash_t found[], size_t count) {
    flash_info_t info;
    if (!port->available) {
        printf("\n%s not scanned: no chip select pin on this board\n", port->name);
        return count;
    }
    switch (flash_chip_identify(port, &info)) {
    case FLASH_KIND_NONE:
        printf("\nNo flash detected on %s\n", port->name);
        return count;
    case FLASH_KIND_UNKNOWN:
        print_unknown(port->name, &info);
        return count;
    case FLASH_KIND_WINBOND:
        break;
    }
    if (flash_id_is_protected(info.status)) {
        print_protected(port->name, &info);
        return count;
    }
    print_erasable(port->name, &info, (char)('A' + count));
    found[count].port = port;
    found[count].info = info;
    return count + 1;
}

static size_t scan_all_ports(erasable_flash_t found[]) {
    size_t count = 0;
    for (size_t i = 0; i < FLASH_PORT_COUNT; i++)
        count = report_port(flash_port_get(i), found, count);
    printf("\n" SEPARATOR "\n\n");
    return count;
}

/* ------------------------------------------------------------------ */
/* Menu                                                                */
/* ------------------------------------------------------------------ */

static void reboot_to_bootsel(void) {
    printf("Rebooting to BOOTSEL\n");
    stdio_flush();
    sleep_ms(500);
    reset_usb_boot(0, 0);
}

/* Returns when the user asks for a scan; BOOT does not return. */
static void wait_for_scan_request(void) {
    char line[LINE_SIZE];
    for (;;) {
        printf("\nFull chip flash erase menu for Raspberry Pico (v%s)\n\n", FLASH_ERASE_VERSION);
        console_read_line("Enter S to start scan or BOOT for boot select > ", line, sizeof(line));
        if (text_is(line, "S"))
            return;
        if (text_is(line, "BOOT"))
            reboot_to_bootsel();
    }
}

/* Index of the chosen chip, or -1 to go back to the start menu. */
static int choose_flash(size_t count) {
    char line[LINE_SIZE];
    if (count == 0) {
        printf("No erasable flash found.\n");
        return -1;
    }
    console_read_line("Option > ", line, sizeof(line));
    return text_option_index(line, count);
}

static void print_erase_warning(const erasable_flash_t *chosen) {
    char unique[ID_TEXT_SIZE];
    uint32_t capacity = flash_id_capacity_bytes(chosen->info.jedec);
    flash_id_format(unique, sizeof(unique), chosen->info.unique_id, UNIQUE_ID_BYTES);
    printf("\nAbout to perform full-chip erase on Flash with Unique ID = %s\n\n", unique);
    if (chosen->port->kind == PORT_QSPI && chosen->port->channel == 0)
        printf("This is the Pico's own program flash: everything stored on it will be lost.\n\n");
    printf("Note that full chip erase may take up to %lu seconds to complete.\n\n",
           (unsigned long)flash_id_erase_max_seconds(capacity));
}

/* True if the user typed the right digits; blank or wrong cancels. */
static bool confirm_erase(const erasable_flash_t *chosen) {
    char line[LINE_SIZE];
    print_erase_warning(chosen);
    console_read_line("Enter last 4 digits of unique ID to start erase, or return to rescan > ",
                      line, sizeof(line));
    if (text_is_blank(line))
        return false;
    if (!flash_id_matches_last4(chosen->info.unique_id, line)) {
        printf("Digits do not match: erase cancelled.\n");
        return false;
    }
    return true;
}

/* Guards against a chip being swapped between the scan and the confirmation. */
static bool same_chip_still_there(const erasable_flash_t *chosen) {
    flash_info_t now;
    if (flash_chip_identify(chosen->port, &now) != FLASH_KIND_WINBOND ||
        memcmp(now.unique_id, chosen->info.unique_id, UNIQUE_ID_BYTES) != 0) {
        printf("The flash on %s has changed since the scan: erase cancelled.\n",
               chosen->port->name);
        return false;
    }
    return true;
}

/* Scans, offers the erasable chips, and erases the one confirmed.
   Returns true to rescan straight away, false to go back to the menu. */
static bool scan_and_erase(void) {
    erasable_flash_t found[FLASH_PORT_COUNT];
    led_off();
    size_t count = scan_all_ports(found);
    int choice = choose_flash(count);
    if (choice < 0)
        return false;
    if (!confirm_erase(&found[choice]) || !same_chip_still_there(&found[choice]))
        return true;
    printf("\n");
    flash_erase_chip(found[choice].port, &found[choice].info);
    return false;
}

int main(void) {
    stdio_init_all();
    flash_ports_init();
    led_init();
    sleep_ms(2000);                 /* time to open a USB terminal */
    for (;;) {
        wait_for_scan_request();
        while (scan_and_erase())
            ;
    }
}
