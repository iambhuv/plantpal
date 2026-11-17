#pragma once

#include <stdbool.h>

void io_init(void);

/* Drive the three status LEDs: green = moisture ok, red = dry, blue = pump. */
void leds_set_status(bool moisture_ok, bool pump_on);

/* True exactly once per button press (debounced). */
bool io_button_pressed(void);

/* Switch the pump relay. No-op while PUMP_HW_VERIFIED is 0. */
void io_pump_set(bool on);
