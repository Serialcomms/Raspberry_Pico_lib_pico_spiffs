/* Host stand-in for the parts of pico/stdlib.h the menu code uses. */
#ifndef SIM_PICO_STDLIB_H
#define SIM_PICO_STDLIB_H
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t absolute_time_t;
#define PICO_ERROR_TIMEOUT (-1)
absolute_time_t get_absolute_time(void);
absolute_time_t make_timeout_time_ms(uint32_t ms);
absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms);
bool time_reached(absolute_time_t t);
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to);
void sleep_ms(uint32_t ms);
bool stdio_init_all(void);
void stdio_flush(void);
int getchar_timeout_us(uint32_t us);
#endif
