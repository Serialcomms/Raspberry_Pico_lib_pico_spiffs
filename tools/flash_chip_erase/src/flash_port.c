/*
 * flash_port.c - transfers on QSPI chip selects 0 and 1 (QMI direct mode)
 * and on the SPI0 and SPI1 peripherals.
 *
 * The program runs from RAM, so nothing uses XIP and the QMI can stay in
 * direct mode. Transfers go straight to the chip: unlike the SDK's
 * flash_do_cmd_cs(), they do not repeat the bootrom's XIP exit sequence each
 * time, so a chip in the middle of an erase only ever sees status reads.
 */
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/structs/qmi.h"
#include "pico/stdlib.h"

#include "flash_port.h"

/* QSPI CS1 GPIO: the board's PSRAM chip select if it has one, else none. */
#ifndef FLASH_ERASE_QSPI_CS1_PIN
#ifdef PICO_PSRAM_CS_PIN
#define FLASH_ERASE_QSPI_CS1_PIN PICO_PSRAM_CS_PIN
#else
#define FLASH_ERASE_QSPI_CS1_PIN (-1)
#endif
#endif

/* SPI0 wiring: matches the lib_winbond_w25q and pico_spiffs defaults. */
#ifndef FLASH_ERASE_SPI0_PIN_MISO
#define FLASH_ERASE_SPI0_PIN_MISO 16
#endif
#ifndef FLASH_ERASE_SPI0_PIN_CS
#define FLASH_ERASE_SPI0_PIN_CS 17
#endif
#ifndef FLASH_ERASE_SPI0_PIN_SCK
#define FLASH_ERASE_SPI0_PIN_SCK 18
#endif
#ifndef FLASH_ERASE_SPI0_PIN_MOSI
#define FLASH_ERASE_SPI0_PIN_MOSI 19
#endif

/* SPI1 wiring. */
#ifndef FLASH_ERASE_SPI1_PIN_MISO
#define FLASH_ERASE_SPI1_PIN_MISO 12
#endif
#ifndef FLASH_ERASE_SPI1_PIN_CS
#define FLASH_ERASE_SPI1_PIN_CS 13
#endif
#ifndef FLASH_ERASE_SPI1_PIN_SCK
#define FLASH_ERASE_SPI1_PIN_SCK 14
#endif
#ifndef FLASH_ERASE_SPI1_PIN_MOSI
#define FLASH_ERASE_SPI1_PIN_MOSI 15
#endif

/* Kept slow: reliable over jumper wires, and only a few bytes are read. */
#ifndef FLASH_ERASE_SPI_BAUD
#define FLASH_ERASE_SPI_BAUD (5 * 1000 * 1000)
#endif

/* QMI direct-mode clock divisor: 150 MHz / 30 = 5 MHz. */
#define QMI_DIRECT_CLKDIV 30

#if FLASH_ERASE_QSPI_CS1_PIN >= 0
#if FLASH_ERASE_QSPI_CS1_PIN != 0 && FLASH_ERASE_QSPI_CS1_PIN != 8 && \
    FLASH_ERASE_QSPI_CS1_PIN != 19 && FLASH_ERASE_QSPI_CS1_PIN != 47
#error "FLASH_ERASE_QSPI_CS1_PIN must be GPIO 0, 8, 19 or 47 (the XIP_CS1 pins)"
#endif
#if FLASH_ERASE_QSPI_CS1_PIN >= NUM_BANK0_GPIOS
#error "FLASH_ERASE_QSPI_CS1_PIN does not exist on this chip"
#endif
#if FLASH_ERASE_QSPI_CS1_PIN == FLASH_ERASE_SPI0_PIN_MOSI
#error "FLASH_ERASE_QSPI_CS1_PIN clashes with FLASH_ERASE_SPI0_PIN_MOSI"
#endif
#endif

static flash_port_t ports[FLASH_PORT_COUNT] = {
    { .name = "QSPI channel 0", .kind = PORT_QSPI, .channel = 0, .pin_cs = -1 },
    { .name = "QSPI channel 1", .kind = PORT_QSPI, .channel = 1,
      .pin_cs = FLASH_ERASE_QSPI_CS1_PIN },
    { .name = "SPI channel 0", .kind = PORT_SPI, .channel = 0,
      .pin_cs = FLASH_ERASE_SPI0_PIN_CS, .pin_sck = FLASH_ERASE_SPI0_PIN_SCK,
      .pin_mosi = FLASH_ERASE_SPI0_PIN_MOSI, .pin_miso = FLASH_ERASE_SPI0_PIN_MISO },
    { .name = "SPI channel 1", .kind = PORT_SPI, .channel = 1,
      .pin_cs = FLASH_ERASE_SPI1_PIN_CS, .pin_sck = FLASH_ERASE_SPI1_PIN_SCK,
      .pin_mosi = FLASH_ERASE_SPI1_PIN_MOSI, .pin_miso = FLASH_ERASE_SPI1_PIN_MISO },
};

/* ------------------------------------------------------------------ */
/* QSPI                                                                */
/* ------------------------------------------------------------------ */

static uint32_t qmi_cs_bit(unsigned int channel) {
    return channel == 0 ? QMI_DIRECT_CSR_ASSERT_CS0N_BITS : QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
}

static void qmi_set_direct_clock(void) {
    hw_write_masked(&qmi_hw->direct_csr, QMI_DIRECT_CLKDIV << QMI_DIRECT_CSR_CLKDIV_LSB,
                    QMI_DIRECT_CSR_CLKDIV_BITS);
}

/* The QMI stalls when DIRECT_RX is full, so the FIFOs need no bounding. */
static void __no_inline_not_in_flash_func(qmi_exchange)(const uint8_t *tx, uint8_t *rx, size_t count) {
    size_t tx_left = count;
    size_t rx_left = count;
    while (tx_left > 0 || rx_left > 0) {
        uint32_t flags = qmi_hw->direct_csr;
        if (tx_left > 0 && !(flags & QMI_DIRECT_CSR_TXFULL_BITS)) {
            qmi_hw->direct_tx = *tx++;
            tx_left--;
        }
        if (rx_left > 0 && !(flags & QMI_DIRECT_CSR_RXEMPTY_BITS)) {
            *rx++ = (uint8_t)qmi_hw->direct_rx;
            rx_left--;
        }
    }
}

static void __no_inline_not_in_flash_func(qspi_transfer)(unsigned int channel, const uint8_t *tx,
                                                         uint8_t *rx, size_t count) {
    hw_set_bits(&qmi_hw->direct_csr, qmi_cs_bit(channel));
    hw_set_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    qmi_exchange(tx, rx, count);
    hw_clear_bits(&qmi_hw->direct_csr, QMI_DIRECT_CSR_EN_BITS);
    hw_clear_bits(&qmi_hw->direct_csr, qmi_cs_bit(channel));
}

/* flash_start_xip() connects the QSPI pins and takes CS0 out of XIP mode. */
static void qspi_init(void) {
    flash_start_xip();
    qmi_set_direct_clock();
    ports[0].available = true;
    if (ports[1].pin_cs >= 0) {
        gpio_set_function((uint)ports[1].pin_cs, GPIO_FUNC_XIP_CS1);
        ports[1].available = true;
    }
}

/* ------------------------------------------------------------------ */
/* SPI                                                                 */
/* ------------------------------------------------------------------ */

static spi_inst_t *spi_for(const flash_port_t *port) {
    return port->channel == 0 ? spi0 : spi1;
}

/* MISO is pulled up, so an empty socket reads back as FFh. */
static void spi_port_init(flash_port_t *port) {
    spi_init(spi_for(port), FLASH_ERASE_SPI_BAUD);
    gpio_set_function((uint)port->pin_sck, GPIO_FUNC_SPI);
    gpio_set_function((uint)port->pin_mosi, GPIO_FUNC_SPI);
    gpio_set_function((uint)port->pin_miso, GPIO_FUNC_SPI);
    gpio_pull_up((uint)port->pin_miso);
    gpio_init((uint)port->pin_cs);
    gpio_put((uint)port->pin_cs, 1);
    gpio_set_dir((uint)port->pin_cs, GPIO_OUT);
    port->available = true;
}

static void spi_transfer(const flash_port_t *port, const uint8_t *tx, uint8_t *rx, size_t count) {
    gpio_put((uint)port->pin_cs, 0);
    spi_write_read_blocking(spi_for(port), tx, rx, count);
    gpio_put((uint)port->pin_cs, 1);
}

/* ------------------------------------------------------------------ */
/* All ports                                                           */
/* ------------------------------------------------------------------ */

void flash_ports_init(void) {
    qspi_init();
    spi_port_init(&ports[2]);
    spi_port_init(&ports[3]);
}

flash_port_t *flash_port_get(size_t index) {
    return index < FLASH_PORT_COUNT ? &ports[index] : NULL;
}

void flash_port_transfer(const flash_port_t *port, const uint8_t *tx, uint8_t *rx, size_t count) {
    if (port->kind == PORT_QSPI)
        qspi_transfer(port->channel, tx, rx, count);
    else
        spi_transfer(port, tx, rx, count);
}
