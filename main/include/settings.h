#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_pick.h"

/* Persist the watering policy, AP switch and saved Wi-Fi list to NVS so they
 * survive a reboot. Call settings_init() once at boot, after nvs_flash_init();
 * it applies the stored policy to watering. Saves skip unchanged values. */
void settings_init(void);

void settings_save_auto(bool on);
void settings_save_threshold(uint8_t pct);
void settings_save_run_s(uint8_t s);
void settings_save_wait_s(uint16_t s);

/* Soft-AP on/off; default true. */
void settings_save_ap_enabled(bool on);
bool settings_ap_enabled(void);

/* Saved networks, one NVS blob. bad_pass is never persisted (false on load).
 * load returns the count copied (<= max); store writes only if changed. */
size_t settings_wifi_load(wifi_saved_t *out, size_t max);
void settings_wifi_store(const wifi_saved_t *list, size_t n);
