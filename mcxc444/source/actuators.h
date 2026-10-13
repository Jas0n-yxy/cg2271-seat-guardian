/*
 * actuators.h - Onboard RGB LED (PWM) and buzzer.
 */
#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <stdint.h>

void actuators_init(void);

/* Each channel 0-100 % brightness */
void led_set_rgb(int red, int green, int blue);

void buzzer_on(void);
void buzzer_off(void);

#endif /* ACTUATORS_H */
