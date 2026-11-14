#include "pico.h"
#define GPIO_OUT 1
static inline void gpio_init(uint pin) { (void)pin; }
static inline void gpio_set_dir(uint pin, int out) { (void)pin; (void)out; }
static inline void gpio_put(uint pin, bool value) { (void)pin; (void)value; }
