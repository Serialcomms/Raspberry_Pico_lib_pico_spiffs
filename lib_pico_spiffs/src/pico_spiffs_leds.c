/*
 * pico_spiffs_leds.c - optional activity LEDs for flash reads, writes and
 * erases. Define PICO_SPIFFS_LED_READ_PIN, PICO_SPIFFS_LED_WRITE_PIN and/or
 * PICO_SPIFFS_LED_ERASE_PIN (GPIO numbers) to use them.
 */
#include "hardware/gpio.h"

#include "pico_spiffs_flash.h"

#if defined(PICO_SPIFFS_LED_READ_PIN) || defined(PICO_SPIFFS_LED_WRITE_PIN) || \
    defined(PICO_SPIFFS_LED_ERASE_PIN)
static void led_init(int pin) {
    gpio_init((uint)pin);
    gpio_set_dir((uint)pin, GPIO_OUT);
}
#endif

void pico_spiffs_leds_init(void) {
#ifdef PICO_SPIFFS_LED_READ_PIN
    led_init(PICO_SPIFFS_LED_READ_PIN);
#endif
#ifdef PICO_SPIFFS_LED_WRITE_PIN
    led_init(PICO_SPIFFS_LED_WRITE_PIN);
#endif
#ifdef PICO_SPIFFS_LED_ERASE_PIN
    led_init(PICO_SPIFFS_LED_ERASE_PIN);
#endif
}

void pico_spiffs_led_read(bool on) {
#ifdef PICO_SPIFFS_LED_READ_PIN
    gpio_put(PICO_SPIFFS_LED_READ_PIN, on);
#else
    (void)on;
#endif
}

void pico_spiffs_led_write(bool on) {
#ifdef PICO_SPIFFS_LED_WRITE_PIN
    gpio_put(PICO_SPIFFS_LED_WRITE_PIN, on);
#else
    (void)on;
#endif
}

void pico_spiffs_led_erase(bool on) {
#ifdef PICO_SPIFFS_LED_ERASE_PIN
    gpio_put(PICO_SPIFFS_LED_ERASE_PIN, on);
#else
    (void)on;
#endif
}
