/* PlantPal — small mutex-guarded snapshot and event log so the HTTP handlers
 * can read the latest values without racing the main loop. */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "state.h"

static sensor_snapshot_t s_snapshot;
static eventlog_t s_events;
static SemaphoreHandle_t s_mtx;

void state_init(void)
{
    s_mtx = xSemaphoreCreateMutex();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
}

void state_publish_sensors(const sensor_snapshot_t *s)
{
    if (!s_mtx || !s) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_snapshot = *s;
    xSemaphoreGive(s_mtx);
}

sensor_snapshot_t state_sensors(void)
{
    sensor_snapshot_t s;
    memset(&s, 0, sizeof(s));

    if (s_mtx) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        s = s_snapshot;
        xSemaphoreGive(s_mtx);
    }
    return s;
}

uint32_t state_uptime_s(void)
{
    return (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
}

void state_log(event_kind_t kind, uint32_t n, const char *s)
{
    if (!s_mtx) {
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    eventlog_push(&s_events, state_uptime_s(), kind, n, s);
    xSemaphoreGive(s_mtx);
}

size_t state_events(event_t *out, size_t max)
{
    size_t n = 0;

    if (s_mtx) {
        xSemaphoreTake(s_mtx, portMAX_DELAY);
        n = eventlog_recent(&s_events, out, max);
        xSemaphoreGive(s_mtx);
    }
    return n;
}
