#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Watering state machine: threshold, run time, wait time, hard cap.
 *
 * Call watering_update() regularly with the latest moisture reading; it
 * returns whether the pump should be running and drives it internally.
 */
bool watering_update(uint8_t moisture_pct);

typedef enum { WATER_SRC_BUTTON, WATER_SRC_PANEL } water_src_t;

/* Queue one manual watering cycle (button press or dashboard). */
void watering_request_manual(water_src_t src);

/* Runtime policy (adjustable from the dashboard). Defaults come from
 * plantpal.h. Reads/writes are single words, so no lock is needed. */
void watering_set_auto(bool on);
bool watering_get_auto(void);
void watering_set_threshold(uint8_t pct);
uint8_t watering_get_threshold(void);
void watering_set_run_s(uint8_t s);   /* clamped to RUN_S_MIN..RUN_S_MAX */
uint8_t watering_get_run_s(void);
void watering_set_wait_s(uint16_t s); /* clamped to WAIT_S_MIN..WAIT_S_MAX */
uint16_t watering_get_wait_s(void);

/* Seconds since the last watering ended, -1 if never. */
int32_t watering_last_watered_s(void);

/* Current pump state, for the status LEDs and the dashboard. */
bool watering_get_pump(void);
