/*
 * led_pulse.c - board LED: off, pulsing (PWM breathing) or steady on.
 *
 * Boards without a plain GPIO LED (such as the Pico 2 W) get empty functions.
 */
#include "pico/stdlib.h"   /* first: defines PICO_DEFAULT_LED_PIN for the board */

#include "led_pulse.h"

#ifdef PICO_DEFAULT_LED_PIN

#include "hardware/irq.h"
#include "hardware/pwm.h"

#define LED_PIN PICO_DEFAULT_LED_PIN
#define PWM_TOP 65535u

/* 150 MHz / 8 / 65536: about 286 wraps a second, so one breath of 2 x 255
   steps takes about 1.8 s. */
#define PWM_CLKDIV 8.0f

static uint slice;
static volatile int fade;
static volatile bool fading_up;

/* Squaring the fade level makes the brightness change look even. */
static void pwm_wrap_handler(void) {
    pwm_clear_irq(slice);
    if (fading_up) {
        if (++fade >= 255)
            fading_up = false;
    } else {
        if (--fade <= 0)
            fading_up = true;
    }
    pwm_set_gpio_level(LED_PIN, (uint16_t)(fade * fade));
}

static void stop_pulse(void) {
    pwm_set_irq_enabled(slice, false);
    pwm_clear_irq(slice);
}

void led_init(void) {
    gpio_set_function(LED_PIN, GPIO_FUNC_PWM);
    slice = pwm_gpio_to_slice_num(LED_PIN);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, PWM_CLKDIV);
    pwm_config_set_wrap(&config, PWM_TOP);
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(LED_PIN, 0);
    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), pwm_wrap_handler);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
}

void led_off(void) {
    stop_pulse();
    pwm_set_gpio_level(LED_PIN, 0);
}

void led_pulse(void) {
    fade = 0;
    fading_up = true;
    pwm_clear_irq(slice);
    pwm_set_irq_enabled(slice, true);
}

void led_steady_on(void) {
    stop_pulse();
    pwm_set_gpio_level(LED_PIN, PWM_TOP);
}

#else

void led_init(void) {}
void led_off(void) {}
void led_pulse(void) {}
void led_steady_on(void) {}

#endif
