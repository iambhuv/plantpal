/* PlantPal — NVS persistence for the runtime watering policy, AP switch and
 * saved Wi-Fi networks. Every save compares with the stored value first so
 * unchanged values never cost a flash write. */
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#include "settings.h"
#include "watering.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE "plantpal"
#define KEY_AUTO      "auto"
#define KEY_THRESHOLD "threshold"
#define KEY_RUN_S     "run_s"
#define KEY_WAIT_S    "wait_s"
#define KEY_AP_ON     "ap_on"
#define KEY_WIFI      "wifi"

static bool s_ap_on = true;

static void save_u8(const char *key, uint8_t value)
{
    nvs_handle_t h;
    uint8_t old;

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed (save %s)", key);
        return;
    }
    if (nvs_get_u8(h, key, &old) != ESP_OK || old != value) {
        if (nvs_set_u8(h, key, value) != ESP_OK || nvs_commit(h) != ESP_OK) {
            ESP_LOGW(TAG, "save %s failed", key);
        }
    }
    nvs_close(h);
}

static void save_u16(const char *key, uint16_t value)
{
    nvs_handle_t h;
    uint16_t old;

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed (save %s)", key);
        return;
    }
    if (nvs_get_u16(h, key, &old) != ESP_OK || old != value) {
        if (nvs_set_u16(h, key, value) != ESP_OK || nvs_commit(h) != ESP_OK) {
            ESP_LOGW(TAG, "save %s failed", key);
        }
    }
    nvs_close(h);
}

void settings_init(void)
{
    nvs_handle_t h;
    uint8_t v;
    uint16_t v16;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings (fresh flash), using defaults");
        return;
    }

    if (nvs_get_u8(h, KEY_AUTO, &v) == ESP_OK) {
        watering_set_auto(v != 0);
    }
    if (nvs_get_u8(h, KEY_THRESHOLD, &v) == ESP_OK) {
        watering_set_threshold(v);
    }
    if (nvs_get_u8(h, KEY_RUN_S, &v) == ESP_OK) {
        watering_set_run_s(v);
    }
    if (nvs_get_u16(h, KEY_WAIT_S, &v16) == ESP_OK) {
        watering_set_wait_s(v16);
    }
    if (nvs_get_u8(h, KEY_AP_ON, &v) == ESP_OK) {
        s_ap_on = (v != 0);
    }
    nvs_close(h);

    ESP_LOGI(TAG, "loaded: auto=%d threshold=%u run=%us wait=%us ap=%d",
             watering_get_auto(), (unsigned)watering_get_threshold(),
             (unsigned)watering_get_run_s(), (unsigned)watering_get_wait_s(), s_ap_on);
}

void settings_save_auto(bool on)
{
    save_u8(KEY_AUTO, on ? 1 : 0);
}

void settings_save_threshold(uint8_t pct)
{
    save_u8(KEY_THRESHOLD, pct);
}

void settings_save_run_s(uint8_t s)
{
    save_u8(KEY_RUN_S, s);
}

void settings_save_wait_s(uint16_t s)
{
    save_u16(KEY_WAIT_S, s);
}

void settings_save_ap_enabled(bool on)
{
    s_ap_on = on;
    save_u8(KEY_AP_ON, on ? 1 : 0);
}

bool settings_ap_enabled(void)
{
    return s_ap_on;
}

size_t settings_wifi_load(wifi_saved_t *out, size_t max)
{
    nvs_handle_t h;
    size_t len = 0, n = 0;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return 0;
    }
    if (nvs_get_blob(h, KEY_WIFI, NULL, &len) == ESP_OK && len % sizeof(wifi_saved_t) == 0) {
        n = len / sizeof(wifi_saved_t);
        wifi_saved_t *all = malloc(len ? len : 1);
        if (all && nvs_get_blob(h, KEY_WIFI, all, &len) == ESP_OK) {
            if (n > max) {
                n = max;
            }
            memcpy(out, all, n * sizeof(wifi_saved_t));
            for (size_t i = 0; i < n; i++) {
                out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
                out[i].pass[sizeof(out[i].pass) - 1] = '\0';
                out[i].bad_pass = false;
            }
        } else {
            n = 0;
        }
        free(all);
    }
    nvs_close(h);
    return n;
}

void settings_wifi_store(const wifi_saved_t *list, size_t n)
{
    nvs_handle_t h;
    size_t len = n * sizeof(wifi_saved_t), old_len = 0;
    /* new list (bad_pass cleared) followed by room for the stored one */
    wifi_saved_t *buf = malloc(2 * len + 1);

    if (!buf) {
        ESP_LOGW(TAG, "save wifi: out of memory");
        return;
    }
    memcpy(buf, list, len);
    for (size_t i = 0; i < n; i++) {
        buf[i].bad_pass = false;
    }

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed (save wifi)");
        free(buf);
        return;
    }
    bool same = nvs_get_blob(h, KEY_WIFI, NULL, &old_len) == ESP_OK && old_len == len &&
                nvs_get_blob(h, KEY_WIFI, buf + n, &old_len) == ESP_OK &&
                memcmp(buf, buf + n, len) == 0;
    if (!same) {
        esp_err_t err = (n == 0) ? nvs_erase_key(h, KEY_WIFI) : nvs_set_blob(h, KEY_WIFI, buf, len);
        if ((err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) || nvs_commit(h) != ESP_OK) {
            ESP_LOGW(TAG, "save wifi failed");
        }
    }
    nvs_close(h);
    free(buf);
}
