/* PlantPal — watering state machine.
 *
 * Rules:
 *   - start when moisture < threshold (auto) or on a manual request
 *   - run for run_s seconds, hard-capped at MAX_WATER_DURATION_MS
 *   - then wait wait_s seconds (and POST_WATER_SETTLE_MS) before auto-watering
 *   - manual requests bypass the threshold but not the hard cap
 *   - each finished run and each fall below threshold goes to the event log
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "plantpal.h"
#include "watering.h"
#include "io.h"
#include "policy.h"
#include "state.h"

static bool     s_pump_on;
static volatile int8_t s_manual_req = -1;    /* water_src_t, -1 = none; one byte so no lock */
static event_kind_t s_run_kind;
static bool     s_dry_armed = true;
static bool     s_ever_watered;
static bool     s_auto = true;
static uint8_t  s_threshold = MOISTURE_THRESHOLD_PCT;
static uint8_t  s_run_s = WATER_DURATION_MS / 1000;
static uint16_t s_wait_s = COOLDOWN_MS / 1000;
static uint32_t s_water_start_ms;
static uint32_t s_last_water_end_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void start_watering(event_kind_t kind, const char *why)
{
    s_pump_on = true;
    s_run_kind = kind;
    s_water_start_ms = now_ms();
    io_pump_set(true);
    printf("watering: start (%s)\n", why);
}

void watering_request_manual(water_src_t src)
{
    s_manual_req = (int8_t)src;
}

void watering_set_run_s(uint8_t s)
{
    s_run_s = (uint8_t)policy_clamp(s, RUN_S_MIN, RUN_S_MAX);
    printf("watering: run %us\n", (unsigned)s_run_s);
}

uint8_t watering_get_run_s(void)
{
    return s_run_s;
}

void watering_set_wait_s(uint16_t s)
{
    s_wait_s = (uint16_t)policy_clamp(s, WAIT_S_MIN, WAIT_S_MAX);
    printf("watering: wait %us\n", (unsigned)s_wait_s);
}

uint16_t watering_get_wait_s(void)
{
    return s_wait_s;
}

int32_t watering_last_watered_s(void)
{
    return s_ever_watered ? (int32_t)((now_ms() - s_last_water_end_ms) / 1000) : -1;
}

void watering_set_auto(bool on)
{
    s_auto = on;
    printf("watering: auto %s\n", on ? "on" : "off");
}

bool watering_get_auto(void)
{
    return s_auto;
}

void watering_set_threshold(uint8_t pct)
{
    if (pct > 100) {
        pct = 100;
    }
    s_threshold = pct;
    printf("watering: threshold %u%%\n", (unsigned)pct);
}

uint8_t watering_get_threshold(void)
{
    return s_threshold;
}

bool watering_get_pump(void)
{
    return s_pump_on;
}

bool watering_update(uint8_t moisture_pct)
{
    uint32_t now = now_ms();

    if (moisture_pct >= s_threshold + 3) {   /* hysteresis: noise around threshold logs once */
        s_dry_armed = true;
    } else if (moisture_pct < s_threshold && s_dry_armed) {
        s_dry_armed = false;
        state_log(EV_SOIL_DRY, moisture_pct, NULL);
    }

    if (s_pump_on) {
        s_manual_req = -1;   /* a press during a run is ignored, not queued for afterwards */
        uint32_t run = now - s_water_start_ms;
        uint32_t limit = (uint32_t)s_run_s * 1000;   /* re-read each tick: a change applies mid-run */

        if (limit > MAX_WATER_DURATION_MS) {
            limit = MAX_WATER_DURATION_MS;
        }

        if (run >= limit) {
            s_pump_on = false;
            io_pump_set(false);
            s_last_water_end_ms = now;
            s_ever_watered = true;
            printf("watering: done (%u ms)\n", (unsigned)run);
            state_log(s_run_kind, run, NULL);
        }
        return s_pump_on;
    }

    uint32_t since = s_ever_watered ? (now - s_last_water_end_ms) : UINT32_MAX;

    int8_t req = s_manual_req;
    if (req >= 0) {
        s_manual_req = -1;
        if (req == WATER_SRC_BUTTON) {
            start_watering(EV_WATER_BUTTON, "button");
        } else {
            start_watering(EV_WATER_PANEL, "panel");
        }
        return true;
    }

    if (s_auto && moisture_pct < s_threshold &&
        since >= (uint32_t)s_wait_s * 1000 && since >= POST_WATER_SETTLE_MS) {
        start_watering(EV_WATER_AUTO, "auto");
        return true;
    }

    return false;
}
