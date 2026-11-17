#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialise ADC and the DHT11 (via the vendored libdriver component). */
void sensors_init(void);

/* Averaged moisture ADC reading, 0..1023 (higher = drier). */
uint16_t sensors_moisture_raw(void);

/* Map a raw reading to 0..100 % (100 = wet). */
uint8_t sensors_moisture_pct(uint16_t raw);

/* Read temperature (C) and humidity (%). Returns false on read error. */
bool sensors_read_dht(float *temp_c, uint8_t *humidity_pct);
