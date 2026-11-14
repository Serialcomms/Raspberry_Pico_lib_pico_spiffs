/* winbond_W25Q_main.h - host stand-in for lib_winbond_w25q (tests only). */
#ifndef HOST_W25Q_H
#define HOST_W25Q_H
#include "pico.h"
typedef struct spi_inst spi_inst_t;
#define spi0 ((spi_inst_t *)0)
typedef struct {
    uint8_t PIN_CS, PIN_SCK, PIN_MOSI, PIN_MISO;
    uint32_t SPI_BAUD, DMA_BYTES;
    spi_inst_t *device;
    uint dma_channel_tx, dma_channel_rx;
    uint32_t dma_channel_mask_tx_rx;
} pico_spi_device_t;
void lib_winbond_init_all(pico_spi_device_t *pico_spi);
int lib_winbond_query_jedec_id(pico_spi_device_t *pico_spi, uint8_t *query_result);
uint32_t lib_winbond_program_flash_page(pico_spi_device_t *pico_spi, uint32_t address, const uint8_t *data, uint16_t length);
uint32_t lib_winbond_erase_4k_sector(pico_spi_device_t *pico_spi, uint32_t address);
uint32_t lib_winbond_erase_64k_block(pico_spi_device_t *pico_spi, uint32_t address);
int lib_winbond_read_data_spi(pico_spi_device_t *pico_spi, uint32_t address, uint8_t *buffer, size_t length);
uint32_t lib_winbond_read_data_dma(pico_spi_device_t *pico_spi, uint32_t address, uint8_t *buffer, size_t length);
#endif
