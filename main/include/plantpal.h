/* PlantPal — shared configuration (pins, thresholds, timings). */
#pragma once

#include "driver/gpio.h"

/* ---- pins (NodeMCU) ---- */
#define PIN_LED_GREEN   GPIO_NUM_4    /* D2  */
#define PIN_LED_RED     GPIO_NUM_12   /* D6  */
#define PIN_LED_BLUE    GPIO_NUM_13   /* D7  */
#define PIN_BUTTON      GPIO_NUM_16   /* D0  */
#define PIN_PUMP        GPIO_NUM_5    /* D1  */
#define PIN_DHT         GPIO_NUM_14   /* D5  — used by components/dht11 HAL */

/* LEDs are active-low (anode -> 3V3, cathode -> GPIO) */
#define LED_ON   0
#define LED_OFF  1

/* ---- moisture ---- */
#define MOISTURE_SAMPLE_MS   2000
#define MOISTURE_RAW_DRY     1023   /* in air */
#define MOISTURE_RAW_WET     500    /* tap-water submerged (~483-500) */

/* ---- watering policy ---- */
#define MOISTURE_THRESHOLD_PCT   40
#define WATER_DURATION_MS        3000    /* default run_s */
#define MAX_WATER_DURATION_MS    10000   /* hard cap */
#define COOLDOWN_MS              60000   /* default wait_s */
#define POST_WATER_SETTLE_MS     5000

/* ---- DHT11 ---- */
#define DHT_SAMPLE_MS  10000

/* ---- pump safety switch ----------------------------------------------
 * 1 = relay is driven (GPIO5 HIGH -> pump on). Set only after verifying the
 *     relay wiring, the 9 V rail polarity, and pump current.
 * -------------------------------------------------------------------- */
#define PUMP_HW_VERIFIED  1
