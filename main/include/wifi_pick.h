#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { char ssid[33]; char pass[65]; bool bad_pass; } wifi_saved_t;
typedef struct { char ssid[33]; int8_t rssi; } wifi_seen_t;

/* index into saved of strongest visible, non-bad_pass entry; skip index `exclude` (-1 = none); -1 if none */
int wifi_pick(const wifi_saved_t *saved, size_t n_saved, const wifi_seen_t *seen, size_t n_seen, int exclude);

/* Auth-failure strikes for one saved entry; reset with (wifi_strikes_t){ -1, 0 }. */
typedef struct { int idx; uint8_t n; } wifi_strikes_t;

/* Record a join failure of saved index idx; true on the second auth failure in a
 * row for the same idx (any other reason or another idx starts over). */
bool wifi_auth_strike(wifi_strikes_t *s, int idx, bool auth_fail);
