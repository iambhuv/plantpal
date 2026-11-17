#include <string.h>

#include "wifi_pick.h"

int wifi_pick(const wifi_saved_t *saved, size_t n_saved, const wifi_seen_t *seen, size_t n_seen, int exclude)
{
    int best = -1;
    int best_rssi = 0;
    for (size_t i = 0; i < n_saved; i++) {
        if ((int)i == exclude || saved[i].bad_pass) continue;
        for (size_t j = 0; j < n_seen; j++) {
            if (strcmp(saved[i].ssid, seen[j].ssid) != 0) continue;
            if (best < 0 || seen[j].rssi > best_rssi) {
                best = (int)i;
                best_rssi = seen[j].rssi;
            }
        }
    }
    return best;
}

bool wifi_auth_strike(wifi_strikes_t *s, int idx, bool auth_fail)
{
    if (!auth_fail || s->idx != idx) {
        s->idx = idx;
        s->n = 0;
    }
    if (auth_fail) {
        s->n++;
    }
    return s->n >= 2;
}
