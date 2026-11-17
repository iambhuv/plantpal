#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* Multi-network Wi-Fi manager: AP "PlantPal" (192.168.4.1) + station that joins
 * the strongest visible saved network, with failover; mDNS "plantpal.local".
 * All Wi-Fi state lives in one task; these calls are safe from any task. */

/* Replaces wifi_ap_init(): nvs_flash_init, wifi + mDNS start, spawns the task. */
void wifi_start(void);

typedef struct { bool sta_up; char ssid[33]; char ip[16]; int8_t rssi; bool ap_up; } wifi_status_t;
wifi_status_t wifi_status(void);

typedef enum { WIFI_NET_CONNECTED, WIFI_NET_IN_RANGE, WIFI_NET_AWAY, WIFI_NET_BAD_PASS } wifi_net_state_t;
typedef struct { char ssid[33]; wifi_net_state_t state; int8_t rssi; } wifi_net_t;

/* Saved list with state from the last scan; no passwords. Returns count. */
size_t wifi_networks(wifi_net_t *out, size_t max);

/* Add or edit a saved network. ssid 1..32 chars; pass "" (open) or 8..64 chars;
 * NULL = keep the existing password (new entry: open). Clears bad_pass.
 * ESP_ERR_INVALID_ARG bad lengths, ESP_ERR_NO_MEM when 5 are saved,
 * ESP_ERR_TIMEOUT if the wifi task did not answer within ~1 s. */
esp_err_t wifi_save_network(const char *ssid, const char *pass_or_null);

/* ESP_ERR_NOT_FOUND if not saved. */
esp_err_t wifi_delete_network(const char *ssid);

/* Persisted "keep AP on" switch. Off: AP still appears after 30 s without a station IP. */
void wifi_set_ap_enabled(bool on);
