/* sim.c - simulated clock, console, LED and four flash ports for running
   the menu on a PC. Input comes from stdin; end of input ends the run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "flash_port.h"
#include "led_pulse.h"

static uint64_t now_us;
absolute_time_t get_absolute_time(void) { now_us += 10; return now_us; }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return now_us + ms * 1000ull; }
absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms) { return t + ms * 1000ull; }
bool time_reached(absolute_time_t t) { now_us += 10; return now_us >= t; }
int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b) { return (int64_t)(b - a); }
void sleep_ms(uint32_t ms) { now_us += ms * 1000ull; }
bool stdio_init_all(void) { return true; }
void stdio_flush(void) { fflush(stdout); }
int getchar_timeout_us(uint32_t us) {
    (void)us;
    int c = getchar();
    if (c == EOF) { printf("\n[end of scripted input]\n"); exit(0); }
    return c;
}
void reset_usb_boot(uint32_t a, uint32_t b) { (void)a; (void)b; printf("[reset_usb_boot]\n"); exit(0); }
void led_init(void) {}
void led_off(void) { printf("[LED off]"); }
void led_pulse(void) { printf("[LED pulse]"); }
void led_steady_on(void) { printf("[LED steady on]\n"); }

/* A simulated W25Q chip. */
typedef struct {
    bool present;
    uint8_t jedec[3];
    uint8_t unique[8];
    uint8_t sr[3];
    uint32_t size;
    uint8_t *mem;
    uint64_t busy_until;
    int chip_erases;
    int unique_reads;
} sim_chip_t;

static sim_chip_t chips[FLASH_PORT_COUNT];
static flash_port_t ports[FLASH_PORT_COUNT] = {
    { "QSPI channel 0", PORT_QSPI, 0, -1, 0, 0, 0, true },
    { "QSPI channel 1", PORT_QSPI, 1, -1, 0, 0, 0, false },
    { "SPI channel 0", PORT_SPI, 0, 17, 18, 19, 16, true },
    { "SPI channel 1", PORT_SPI, 1, 13, 14, 15, 12, true },
};

static void make_chip(sim_chip_t *c, uint8_t cap, uint8_t last, uint8_t sr1) {
    c->present = true;
    c->jedec[0] = 0xEF; c->jedec[1] = 0x40; c->jedec[2] = cap;
    for (int i = 0; i < 8; i++) c->unique[i] = (uint8_t)(0x11 * (i + 1));
    c->unique[7] = last;
    c->sr[0] = sr1; c->sr[1] = 0x02; c->sr[2] = 0x60;
    c->size = 1u << cap;
    c->mem = malloc(c->size);
    memset(c->mem, 0x5A, c->size);   /* not blank before the erase */
}

void flash_ports_init(void) {
    const char *setup = getenv("SIM_SETUP");
    make_chip(&chips[0], 0x17, 0x88, 0x00);           /* W25Q64 onboard */
    if (setup && strchr(setup, 'P'))
        make_chip(&chips[3], 0x18, 0x99, 0x1C);       /* protected W25Q128 on SPI1 */
    if (setup && strchr(setup, 'L'))
        make_chip(&chips[2], 0x19, 0x42, 0x00);       /* W25Q256 on SPI0 */
    else if (setup && strchr(setup, 'S'))
        make_chip(&chips[2], 0x18, 0x42, 0x00);       /* W25Q128 on SPI0 */
}

flash_port_t *flash_port_get(size_t i) { return i < FLASH_PORT_COUNT ? &ports[i] : NULL; }

static bool busy(const sim_chip_t *c) { return now_us < c->busy_until; }

static void chip_command(sim_chip_t *c, const uint8_t *tx, uint8_t *rx, size_t n) {
    memset(rx, 0xFF, n);
    if (!c->present) return;
    uint8_t cmd = tx[0];
    if (busy(c) && cmd != 0x05 && cmd != 0x35 && cmd != 0x15) return;  /* ignored while busy */
    switch (cmd) {
    case 0x9F: memcpy(&rx[1], c->jedec, n - 1 < 3 ? n - 1 : 3); break;
    case 0x90: rx[4] = 0xEF; rx[5] = (uint8_t)(c->jedec[2] - 1); break;
    case 0x4B:
        memcpy(&rx[5], c->unique, 8);
        if (getenv("SIM_SWAP") && ++c->unique_reads > 1)
            rx[12] ^= 0x01;   /* a different chip after the scan */
        break;
    case 0x05: rx[1] = (uint8_t)((c->sr[0] & ~1u) | (busy(c) ? 1 : 0)); break;
    case 0x35: rx[1] = c->sr[1]; break;
    case 0x15: rx[1] = c->sr[2]; break;
    case 0x06: c->sr[0] |= 0x02; break;
    case 0x60:
        if ((c->sr[0] & 0x02) && !(c->sr[0] & 0x1C)) {
            c->chip_erases++;
            memset(c->mem, 0xFF, c->size);
            c->busy_until = now_us + (getenv("SIM_ERASE_US") ? strtoull(getenv("SIM_ERASE_US"), NULL, 10) : 3500000ull);
        }
        c->sr[0] &= (uint8_t)~0x02;
        break;
    case 0x03: case 0x13: {
        size_t hdr = cmd == 0x13 ? 5 : 4;
        uint32_t a = cmd == 0x13 ? (uint32_t)tx[1] << 24 | tx[2] << 16 | tx[3] << 8 | tx[4]
                                 : (uint32_t)tx[1] << 16 | tx[2] << 8 | tx[3];
        for (size_t i = hdr; i < n; i++) rx[i] = c->mem[(a + i - hdr) % c->size];
        break;
    }
    }
}

void flash_port_transfer(const flash_port_t *port, const uint8_t *tx, uint8_t *rx, size_t count) {
    chip_command(&chips[port - ports], tx, rx, count);
}

void sim_report(void) __attribute__((destructor));
void sim_report(void) {
    for (int i = 0; i < FLASH_PORT_COUNT; i++)
        if (chips[i].present)
            printf("[sim] %s: chip erases = %d\n", ports[i].name, chips[i].chip_erases);
}
