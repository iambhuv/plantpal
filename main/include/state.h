#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "eventlog.h"

/* Latest sensor snapshot, shared between the main loop (writer) and the
 * HTTP server task (reader) through a mutex. */
typedef struct {
    uint16_t moisture_raw;
    uint8_t  moisture_pct;
    float    temp_c;
    uint8_t  humidity;
    bool     have_dht;
} sensor_snapshot_t;

void state_init(void);
void state_publish_sensors(const sensor_snapshot_t *s);
sensor_snapshot_t state_sensors(void);

/* Seconds since boot. */
uint32_t state_uptime_s(void);

/* Recent-events log, mutex-guarded so any task may log or read.
 * s may be NULL. state_events() copies newest first, returns the count. */
void state_log(event_kind_t kind, uint32_t n, const char *s);
size_t state_events(event_t *out, size_t max);
