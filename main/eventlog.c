#include <string.h>

#include "eventlog.h"

/* head = slot the next push writes to. */
void eventlog_push(eventlog_t *log, uint32_t t_s, event_kind_t kind, uint32_t n, const char *s)
{
    event_t *e = &log->items[log->head];
    e->t_s = t_s;
    e->kind = kind;
    e->n = n;
    strncpy(e->s, s ? s : "", sizeof e->s - 1);
    e->s[sizeof e->s - 1] = '\0';
    log->head = (log->head + 1) % EVENTLOG_CAP;
    if (log->count < EVENTLOG_CAP) log->count++;
}

size_t eventlog_recent(const eventlog_t *log, event_t *out, size_t max)
{
    size_t k = log->count < max ? log->count : max;
    for (size_t i = 0; i < k; i++)
        out[i] = log->items[(log->head + EVENTLOG_CAP - 1 - i) % EVENTLOG_CAP];
    return k;
}

const char *eventlog_kind_name(event_kind_t k)
{
    static const char *const names[] = {
        "water_auto", "water_button", "water_panel", "soil_dry",
        "wifi_joined", "wifi_lost", "settings",
    };
    return (unsigned)k < sizeof names / sizeof names[0] ? names[k] : "unknown";
}
