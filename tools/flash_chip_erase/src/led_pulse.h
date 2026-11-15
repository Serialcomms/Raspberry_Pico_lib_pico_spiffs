/*
 * led_pulse.h - board LED: off, pulsing (PWM breathing) or steady on.
 *
 * The pulse runs from the PWM wrap interrupt, so it keeps going while the
 * main loop waits on a flash chip.
 */
#ifndef LED_PULSE_H
#define LED_PULSE_H

void led_init(void);
void led_off(void);
void led_pulse(void);
void led_steady_on(void);

#endif
