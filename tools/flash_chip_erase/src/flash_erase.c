/*
 * flash_erase.c - full chip erase with progress dots, LED pulse and a
 * blank check afterwards.
 *
 * Progress bar, redrawn in place with a carriage return:
 *
 *   ERASING FLASH [**************....................................]  14 s
 *
 * A chip erase gives no progress of its own, so stars fill against the
 * typical erase time from the datasheet. If the chip takes longer, the bar
 * waits one star short of full until the chip reports it has finished.
 */
#include <stdio.h>

#include "pico/stdlib.h"

#include "flash_chip.h"
#include "flash_erase.h"
#include "led_pulse.h"

#define BUSY_START_TIMEOUT_MS 100
#define POLL_INTERVAL_MS      100
#define BAR_WIDTH             50
#define BLANK_CHECK_BYTES     FLASH_CHIP_MAX_READ

static void draw_bar(uint32_t stars, uint32_t seconds) {
    printf("\rERASING FLASH [");
    for (uint32_t i = 0; i < BAR_WIDTH; i++)
        putchar(i < stars ? '*' : '.');
    printf("] %3lu s", (unsigned long)seconds);
    stdio_flush();
}

/* Stars for the time so far; never full until the chip is actually done. */
static uint32_t stars_for(uint32_t elapsed_ms, uint32_t typical_ms) {
    uint32_t stars = (uint32_t)((uint64_t)elapsed_ms * BAR_WIDTH / typical_ms);
    return stars < BAR_WIDTH ? stars : BAR_WIDTH - 1;
}

static uint32_t ms_since(absolute_time_t start) {
    return (uint32_t)(absolute_time_diff_us(start, get_absolute_time()) / 1000);
}

static bool wait_for_busy(const flash_port_t *port) {
    absolute_time_t give_up = make_timeout_time_ms(BUSY_START_TIMEOUT_MS);
    while (!time_reached(give_up)) {
        if (flash_chip_is_busy(port))
            return true;
    }
    return false;
}

/* Redraws the bar only when the star count or the seconds change. */
static void update_bar(uint32_t elapsed_ms, uint32_t typical_ms, uint32_t *shown) {
    uint32_t stars = stars_for(elapsed_ms, typical_ms);
    uint32_t seconds = elapsed_ms / 1000u;
    uint32_t key = stars * 100000u + seconds;
    if (key != *shown) {
        draw_bar(stars, seconds);
        *shown = key;
    }
}

/* Polls the status register until the chip is idle or timeout_s passes. */
static bool wait_for_idle(const flash_port_t *port, uint32_t typical_s, uint32_t timeout_s,
                          uint32_t *elapsed_ms) {
    absolute_time_t start = get_absolute_time();
    absolute_time_t give_up = delayed_by_ms(start, timeout_s * 1000u);
    uint32_t shown = UINT32_MAX;
    bool idle = false;
    while (!idle && !time_reached(give_up)) {
        sleep_ms(POLL_INTERVAL_MS);
        update_bar(ms_since(start), typical_s * 1000u, &shown);
        idle = !flash_chip_is_busy(port);
    }
    *elapsed_ms = ms_since(start);
    if (idle)
        draw_bar(BAR_WIDTH, *elapsed_ms / 1000u);
    return idle;
}

static bool start_erase(const flash_port_t *port) {
    if (!flash_chip_write_enable(port)) {
        printf("Write enable was not accepted: erase not started.\n");
        return false;
    }
    flash_chip_start_erase(port);
    if (!wait_for_busy(port)) {
        printf("The chip did not start erasing (it may be write protected).\n");
        return false;
    }
    draw_bar(0, 0);
    return true;
}

static bool block_is_blank(const flash_port_t *port, uint32_t address) {
    uint8_t data[BLANK_CHECK_BYTES];
    flash_chip_read(port, address, data, sizeof(data));
    for (size_t i = 0; i < sizeof(data); i++) {
        if (data[i] != 0xFF)
            return false;
    }
    return true;
}

/* Samples the start, middle and end of the chip. */
static bool reads_blank(const flash_port_t *port, uint32_t capacity) {
    return block_is_blank(port, 0) &&
           block_is_blank(port, capacity / 2) &&
           block_is_blank(port, capacity - BLANK_CHECK_BYTES);
}

static bool report_result(const flash_port_t *port, uint32_t capacity, bool idle,
                          uint32_t elapsed_ms) {
    if (!idle) {
        printf("\nErase did not finish in time: the chip is still busy.\n");
        return false;
    }
    printf("\nErase complete in %lu.%lu s.\n", (unsigned long)(elapsed_ms / 1000),
           (unsigned long)(elapsed_ms % 1000 / 100));
    if (!reads_blank(port, capacity)) {
        printf("Blank check FAILED: the chip does not read back as erased.\n");
        return false;
    }
    printf("Blank check passed (start, middle and end read back as FFh).\n");
    return true;
}

bool flash_erase_chip(const flash_port_t *port, const flash_info_t *info) {
    uint32_t capacity = flash_id_capacity_bytes(info->jedec);
    uint32_t typical_s = flash_id_erase_typical_seconds(capacity);
    uint32_t timeout_s = flash_id_erase_max_seconds(capacity) * 3u / 2u;
    uint32_t elapsed_ms = 0;
    led_off();
    if (!start_erase(port))
        return false;
    led_pulse();
    bool idle = wait_for_idle(port, typical_s, timeout_s, &elapsed_ms);
    bool ok = report_result(port, capacity, idle, elapsed_ms);
    if (ok)
        led_steady_on();
    else
        led_off();
    return ok;
}
