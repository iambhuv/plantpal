#pragma once

#include <stddef.h>
#include <stdint.h>

/* Fixed-size ring of recent events; oldest overwritten when full. No SDK deps. */
#define EVENTLOG_CAP 16

typedef enum { EV_WATER_AUTO, EV_WATER_BUTTON, EV_WATER_PANEL, EV_SOIL_DRY,
               EV_WIFI_JOINED, EV_WIFI_LOST, EV_SETTINGS } event_kind_t;

typedef struct { uint32_t t_s; event_kind_t kind; uint32_t n; char s[33]; } event_t;
typedef struct { event_t items[EVENTLOG_CAP]; uint8_t head, count; } eventlog_t;

/* s may be NULL (stored as ""); truncated to 32 chars. */
void eventlog_push(eventlog_t *log, uint32_t t_s, event_kind_t kind, uint32_t n, const char *s);
/* newest first; returns count copied */
size_t eventlog_recent(const eventlog_t *log, event_t *out, size_t max);
const char *eventlog_kind_name(event_kind_t k);
