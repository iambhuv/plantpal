#pragma once

/* Bounds for user-adjustable watering settings (seconds). */
#define RUN_S_MIN 1
#define RUN_S_MAX 10
#define WAIT_S_MIN 10
#define WAIT_S_MAX 1800

static inline long policy_clamp(long v, long lo, long hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
