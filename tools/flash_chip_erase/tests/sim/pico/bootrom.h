#ifndef SIM_PICO_BOOTROM_H
#define SIM_PICO_BOOTROM_H
#include <stdint.h>
void reset_usb_boot(uint32_t gpio_mask, uint32_t disable_mask);
#endif
