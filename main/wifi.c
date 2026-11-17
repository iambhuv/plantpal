/* PlantPal — multi-network Wi-Fi manager + mDNS.
 *
 * One "wifi" task owns every bit of Wi-Fi state. WIFI_EVENT/IP_EVENT handlers
 * and the public API only post messages to its queue; save/delete wait up to
 * 1 s for the task's answer. The task holds s_lock while it handles a message,
 * so wifi_status()/wifi_networks() copy a consistent view under the same lock.
 *
 * Station cycle: scan -> wifi_pick -> connect (8 s join timeout) -> next pick;
 * nothing pickable -> rescan in 5 s (30 s while a phone is on the AP and the
 * station has no IP; never while every entry has a wrong password). A drop after having an IP retries the same
 * network once, then rescans. AP is up iff the switch is on, the list is
 * empty, or the station has had no IP for 30 s.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "mdns.h"

#include "wifi.h"
#include "wifi_pick.h"
#include "settings.h"
#include "state.h"

#define AP_SSID         "PlantPal"
#define AP_PASS         "water1234"
#define AP_CHANNEL      1

#define MAX_SAVED       5
#define MAX_SEEN        12
#define JOIN_TIMEOUT_MS 8000
#define RESCAN_MS       5000
#define RESCAN_AP_MS    30000   /* scans disturb the AP; back off while someone uses it */
#define SCAN_TIMEOUT_MS 10000   /* watchdog in case SCAN_DONE is lost */
#define AP_FALLBACK_MS  30000
#define TICK_MS         1000
#define REPLY_MS        1000

static const char *TAG = "wifi";

typedef enum { M_SCAN_DONE, M_DISCONNECTED, M_GOT_IP, M_SAVE, M_DELETE, M_AP } msg_type_t;

typedef struct {
    msg_type_t type;
    uint8_t reason;     /* M_DISCONNECTED */
    bool flag;          /* M_SAVE: has_pass; M_AP: on */
    char ssid[33];      /* M_DISCONNECTED, M_SAVE, M_DELETE */
    char pass[65];      /* M_SAVE */
    char ip[16];        /* M_GOT_IP */
} msg_t;

typedef enum { PH_IDLE, PH_WAIT, PH_SCANNING, PH_JOINING, PH_CONNECTED } phase_t;

static QueueHandle_t s_q, s_reply;
static SemaphoreHandle_t s_lock, s_api;

/* Owned by the wifi task; readers copy under s_lock. */
static wifi_saved_t s_saved[MAX_SAVED];
static size_t s_n_saved;
static wifi_seen_t s_seen[MAX_SEEN];
static size_t s_n_seen;
static wifi_status_t s_status;
static phase_t s_phase;
static int s_cur = -1;          /* index into s_saved being joined / joined */
static uint8_t s_tried;         /* bitmask of saved entries tried since last scan */
static bool s_retrying;         /* current join is the one retry after a drop */
static TickType_t s_deadline, s_no_ip_since;
static wifi_strikes_t s_strikes = { -1, 0 };   /* bad_pass needs two auth failures in a row */

static wifi_saved_t s_scratch[MAX_SAVED];   /* wifi_pick input; static to spare the stack */

static TickType_t ms(uint32_t m) { return pdMS_TO_TICKS(m); }

static bool due(TickType_t t) { return (int32_t)(xTaskGetTickCount() - t) >= 0; }

static void set_phase(phase_t p, uint32_t timeout_ms)
{
    s_phase = p;
    s_deadline = xTaskGetTickCount() + ms(timeout_ms);
}

static uint32_t rescan_ms(void)
{
    wifi_sta_list_t l;
    bool ap_client = s_status.ap_up && esp_wifi_ap_get_sta_list(&l) == ESP_OK && l.num > 0;
    return (ap_client && !s_status.sta_up) ? RESCAN_AP_MS : RESCAN_MS;
}

static bool all_bad_pass(void)
{
    for (size_t i = 0; i < s_n_saved; i++) {
        if (!s_saved[i].bad_pass) {
            return false;
        }
    }
    return true;
}

static void start_scan(void)
{
    if (all_bad_pass()) {   /* also the empty list; a save/edit clears bad_pass and rescans */
        set_phase(PH_IDLE, 0);
        return;
    }
    wifi_scan_config_t sc = { 0 };   /* all channels, hidden networks not shown */
    if (esp_wifi_scan_start(&sc, false) == ESP_OK) {
        s_tried = 0;
        set_phase(PH_SCANNING, SCAN_TIMEOUT_MS);
    } else {
        uint32_t wait = rescan_ms();
        ESP_LOGW(TAG, "scan start failed, retry in %u s", (unsigned)(wait / 1000));
        set_phase(PH_WAIT, wait);
    }
}

static void connect_to(int i)
{
    wifi_config_t wc = { 0 };

    s_cur = i;
    esp_wifi_disconnect();   /* drop any half-finished attempt; its event (ASSOC_LEAVE) is ignored */
    memcpy(wc.sta.ssid, s_saved[i].ssid, strnlen(s_saved[i].ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, s_saved[i].pass, strnlen(s_saved[i].pass, sizeof(wc.sta.password)));
    if (esp_wifi_set_config(ESP_IF_WIFI_STA, &wc) != ESP_OK || esp_wifi_connect() != ESP_OK) {
        ESP_LOGW(TAG, "connect to %s failed to start", s_saved[i].ssid);
        set_phase(PH_WAIT, RESCAN_MS);
        return;
    }
    ESP_LOGI(TAG, "joining %s%s", s_saved[i].ssid, s_retrying ? " (retry)" : "");
    set_phase(PH_JOINING, JOIN_TIMEOUT_MS);
}

static void try_next(void)
{
    memcpy(s_scratch, s_saved, s_n_saved * sizeof(wifi_saved_t));
    for (size_t i = 0; i < s_n_saved; i++) {
        if (s_tried & (1u << i)) {
            s_scratch[i].bad_pass = true;   /* skip: already tried this round */
        }
    }
    int i = wifi_pick(s_scratch, s_n_saved, s_seen, s_n_seen, -1);
    if (i < 0) {
        uint32_t wait = rescan_ms();
        ESP_LOGI(TAG, "no saved network in range, rescan in %u s", (unsigned)(wait / 1000));
        s_cur = -1;
        set_phase(PH_WAIT, wait);
        return;
    }
    connect_to(i);
}

static void join_failed(bool bad_pass)
{
    if (bad_pass) {
        s_saved[s_cur].bad_pass = true;
        ESP_LOGW(TAG, "%s: wrong password, skipping until edited", s_saved[s_cur].ssid);
    }
    esp_wifi_disconnect();
    if (s_retrying) {
        s_retrying = false;
        start_scan();
        return;
    }
    s_tried |= 1u << s_cur;
    try_next();
}

static void lost_ip(void)
{
    if (s_status.sta_up) {
        ESP_LOGI(TAG, "lost %s", s_status.ssid);
        state_log(EV_WIFI_LOST, 0, s_status.ssid);
    }
    s_status.sta_up = false;
    s_status.ip[0] = '\0';
    s_status.rssi = 0;
    s_no_ip_since = xTaskGetTickCount();
}

static void on_scan_done(void)
{
    uint16_t n = MAX_SEEN;
    wifi_ap_record_t *recs = malloc(MAX_SEEN * sizeof(wifi_ap_record_t));

    s_n_seen = 0;
    if (recs && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
        for (uint16_t i = 0; i < n && s_n_seen < MAX_SEEN; i++) {
            if (recs[i].ssid[0] == '\0') {
                continue;   /* hidden network: never matchable */
            }
            memcpy(s_seen[s_n_seen].ssid, recs[i].ssid, sizeof(s_seen[0].ssid));
            s_seen[s_n_seen].ssid[sizeof(s_seen[0].ssid) - 1] = '\0';
            s_seen[s_n_seen].rssi = recs[i].rssi;
            s_n_seen++;
        }
    }
    free(recs);
    if (s_phase == PH_SCANNING) {
        try_next();
    }
}

static bool is_bad_pass_reason(uint8_t r)
{
    return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           r == WIFI_REASON_HANDSHAKE_TIMEOUT;
}

static void on_disconnected(const msg_t *m)
{
    if (s_phase == PH_CONNECTED) {
        lost_ip();
        s_retrying = true;
        connect_to(s_cur);
    } else if (s_phase == PH_JOINING) {
        /* stale events from an attempt we abandoned or our own disconnect */
        if (m->reason == WIFI_REASON_ASSOC_LEAVE || strcmp(m->ssid, s_saved[s_cur].ssid) != 0) {
            return;
        }
        ESP_LOGI(TAG, "join %s failed (reason %u)", s_saved[s_cur].ssid, m->reason);
        join_failed(wifi_auth_strike(&s_strikes, s_cur, is_bad_pass_reason(m->reason)));
    }
}

static void on_got_ip(const msg_t *m)
{
    /* only a join in progress may claim an IP; a late event from an abandoned
     * attempt would otherwise be credited to the network we moved on to */
    if (s_phase != PH_JOINING || s_cur < 0) {
        return;
    }
    s_phase = PH_CONNECTED;
    s_retrying = false;
    s_strikes = (wifi_strikes_t){ -1, 0 };
    s_status.sta_up = true;
    strcpy(s_status.ssid, s_saved[s_cur].ssid);
    strcpy(s_status.ip, m->ip);
    state_log(EV_WIFI_JOINED, 0, s_status.ssid);
    printf("wifi: joined %s ip %s -> http://plantpal.local\n", s_status.ssid, s_status.ip);
}

static int find_saved(const char *ssid)
{
    for (size_t i = 0; i < s_n_saved; i++) {
        if (strcmp(s_saved[i].ssid, ssid) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static esp_err_t on_save(const msg_t *m)
{
    int i = find_saved(m->ssid);

    if (i < 0) {
        if (s_n_saved >= MAX_SAVED) {
            return ESP_ERR_NO_MEM;
        }
        i = (int)s_n_saved++;
        memset(&s_saved[i], 0, sizeof(s_saved[i]));
        strcpy(s_saved[i].ssid, m->ssid);
    }
    if (m->flag) {
        strcpy(s_saved[i].pass, m->pass);
    }
    s_saved[i].bad_pass = false;
    settings_wifi_store(s_saved, s_n_saved);
    ESP_LOGI(TAG, "saved %s (%u networks)", s_saved[i].ssid, (unsigned)s_n_saved);
    if (s_phase == PH_IDLE || s_phase == PH_WAIT) {
        start_scan();
    }
    return ESP_OK;
}

static esp_err_t on_delete(const msg_t *m)
{
    int i = find_saved(m->ssid);

    if (i < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    bool was_cur = (i == s_cur);
    if (was_cur && (s_phase == PH_CONNECTED || s_phase == PH_JOINING)) {
        lost_ip();
        esp_wifi_disconnect();
    }
    memmove(&s_saved[i], &s_saved[i + 1], (s_n_saved - i - 1) * sizeof(wifi_saved_t));
    s_n_saved--;
    s_tried = (s_tried & ((1u << i) - 1)) | ((s_tried >> (i + 1)) << i);
    if (s_cur > i) {
        s_cur--;
    } else if (was_cur) {
        s_cur = -1;
        s_retrying = false;
    }
    settings_wifi_store(s_saved, s_n_saved);
    ESP_LOGI(TAG, "deleted %s (%u networks)", m->ssid, (unsigned)s_n_saved);
    if (s_n_saved == 0) {
        esp_wifi_scan_stop();
        set_phase(PH_IDLE, 0);
    } else if (was_cur && s_phase != PH_SCANNING) {
        start_scan();
    }
    return ESP_OK;
}

static void update_ap(void)
{
    bool want = s_n_saved == 0 || settings_ap_enabled() ||
                (!s_status.sta_up && (xTaskGetTickCount() - s_no_ip_since) >= ms(AP_FALLBACK_MS));
    /* a mode switch can abort a scan or join; wait for WAIT/CONNECTED/IDLE (checked every <= 1 s) */
    if (want == s_status.ap_up || s_phase == PH_SCANNING || s_phase == PH_JOINING) {
        return;
    }
    if (esp_wifi_set_mode(want ? WIFI_MODE_APSTA : WIFI_MODE_STA) == ESP_OK) {
        s_status.ap_up = want;
        ESP_LOGI(TAG, "AP %s", want ? "on: PlantPal -> http://192.168.4.1" : "off");
    }
}

static void handle(const msg_t *m)
{
    esp_err_t err;

    switch (m->type) {
    case M_SCAN_DONE:    on_scan_done(); break;
    case M_DISCONNECTED: on_disconnected(m); break;
    case M_GOT_IP:       on_got_ip(m); break;
    case M_AP:           settings_save_ap_enabled(m->flag); break;
    case M_SAVE:
    case M_DELETE:
        s_strikes = (wifi_strikes_t){ -1, 0 };   /* edit resets; delete shifts indexes */
        err = (m->type == M_SAVE) ? on_save(m) : on_delete(m);
        xQueueSend(s_reply, &err, 0);
        break;
    }
    if (m->type == M_SAVE || m->type == M_DELETE || m->type == M_AP) {
        /* NVS writes are this task's deepest stack use; measure on device */
        ESP_LOGI(TAG, "stack free %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

static void on_timeout(void)
{
    switch (s_phase) {
    case PH_WAIT:
    case PH_SCANNING:
        start_scan();
        break;
    case PH_JOINING:
        ESP_LOGI(TAG, "join %s timed out", s_saved[s_cur].ssid);
        join_failed(false);
        break;
    default:
        break;
    }
}

static void wifi_task(void *arg)
{
    msg_t m;

    for (;;) {
        bool got = xQueueReceive(s_q, &m, ms(TICK_MS)) == pdTRUE;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (got) {
            handle(&m);
        }
        if (s_phase != PH_IDLE && s_phase != PH_CONNECTED && due(s_deadline)) {
            on_timeout();
        }
        update_ap();
        if (s_status.sta_up) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                s_status.rssi = ap.rssi;
            }
        }
        xSemaphoreGive(s_lock);
    }
}

static void post(const msg_t *m)
{
    if (xQueueSend(s_q, m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, event %d dropped", m->type);   /* timeouts recover */
    }
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    msg_t m = { 0 };

    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        m.type = M_SCAN_DONE;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        m.type = M_DISCONNECTED;
        m.reason = d->reason;
        memcpy(m.ssid, d->ssid, d->ssid_len < 32 ? d->ssid_len : 32);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *g = data;
        m.type = M_GOT_IP;
        snprintf(m.ip, sizeof(m.ip), IPSTR, IP2STR(&g->ip_info.ip));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "AP: client joined " MACSTR, MAC2STR(e->mac));
        return;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "AP: client left " MACSTR, MAC2STR(e->mac));
        return;
    } else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ESP_LOGI(TAG, "AP: client got an IP");
        return;
    } else {
        return;
    }
    post(&m);
}

/* Serialised request/response with the wifi task. */
static esp_err_t call(const msg_t *m)
{
    esp_err_t err = ESP_ERR_TIMEOUT;

    if (!s_q) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_api, portMAX_DELAY);
    xQueueReset(s_reply);   /* drop a late answer to a call that timed out */
    if (xQueueSend(s_q, m, ms(REPLY_MS)) == pdTRUE) {
        xQueueReceive(s_reply, &err, ms(REPLY_MS));
    }
    xSemaphoreGive(s_api);
    return err;
}

esp_err_t wifi_save_network(const char *ssid, const char *pass_or_null)
{
    msg_t m = { .type = M_SAVE };
    size_t sl = ssid ? strlen(ssid) : 0;
    size_t pl = pass_or_null ? strlen(pass_or_null) : 0;

    if (sl < 1 || sl > 32 || (pl != 0 && (pl < 8 || pl > 64))) {
        return ESP_ERR_INVALID_ARG;
    }
    strcpy(m.ssid, ssid);
    if (pass_or_null) {
        m.flag = true;
        strcpy(m.pass, pass_or_null);
    }
    esp_err_t err = call(&m);
    memset(m.pass, 0, sizeof(m.pass));
    return err;
}

esp_err_t wifi_delete_network(const char *ssid)
{
    msg_t m = { .type = M_DELETE };

    if (!ssid || strlen(ssid) < 1 || strlen(ssid) > 32) {
        return ESP_ERR_INVALID_ARG;
    }
    strcpy(m.ssid, ssid);
    return call(&m);
}

void wifi_set_ap_enabled(bool on)
{
    msg_t m = { .type = M_AP, .flag = on };

    if (s_q) {
        xQueueSend(s_q, &m, ms(REPLY_MS));
    }
}

wifi_status_t wifi_status(void)
{
    wifi_status_t st;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    st = s_status;
    xSemaphoreGive(s_lock);
    return st;
}

size_t wifi_networks(wifi_net_t *out, size_t max)
{
    size_t n;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    n = s_n_saved < max ? s_n_saved : max;
    for (size_t i = 0; i < n; i++) {
        strcpy(out[i].ssid, s_saved[i].ssid);
        out[i].rssi = 0;
        if (s_status.sta_up && (int)i == s_cur) {
            out[i].state = WIFI_NET_CONNECTED;
            out[i].rssi = s_status.rssi;
            continue;
        }
        out[i].state = s_saved[i].bad_pass ? WIFI_NET_BAD_PASS : WIFI_NET_AWAY;
        for (size_t j = 0; j < s_n_seen; j++) {
            if (strcmp(s_seen[j].ssid, s_saved[i].ssid) == 0) {
                out[i].rssi = s_seen[j].rssi;
                if (!s_saved[i].bad_pass) {
                    out[i].state = WIFI_NET_IN_RANGE;
                }
                break;
            }
        }
    }
    xSemaphoreGive(s_lock);
    return n;
}

void wifi_start(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unreadable (%s), erasing: settings and networks reset", esp_err_to_name(nvs));
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    s_q = xQueueCreate(6, sizeof(msg_t));
    s_reply = xQueueCreate(1, sizeof(esp_err_t));
    s_lock = xSemaphoreCreateMutex();
    s_api = xSemaphoreCreateMutex();
    configASSERT(s_q && s_reply && s_lock && s_api);

    /* Entries with an empty SSID would match hidden networks in wifi_pick. */
    wifi_saved_t loaded[MAX_SAVED];
    size_t n = settings_wifi_load(loaded, MAX_SAVED);
    for (size_t i = 0; i < n; i++) {
        if (loaded[i].ssid[0] != '\0') {
            s_saved[s_n_saved++] = loaded[i];
        }
    }

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* RAM: no SDK auto-connect at boot, no flash wear, passwords only in our NVS blob */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &event_handler, NULL));

    wifi_config_t wc = { 0 };
    strcpy((char *)wc.ap.ssid, AP_SSID);
    wc.ap.ssid_len = strlen(AP_SSID);
    strcpy((char *)wc.ap.password, AP_PASS);
    wc.ap.channel = AP_CHANNEL;
    wc.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wc.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_status.ap_up = true;
    s_no_ip_since = xTaskGetTickCount();
    ESP_LOGI(TAG, "AP up: SSID=%s -> http://192.168.4.1, %u saved networks",
             AP_SSID, (unsigned)s_n_saved);

    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        if ((err = mdns_hostname_set("plantpal")) != ESP_OK) {
            ESP_LOGW(TAG, "mdns_hostname_set failed: %s", esp_err_to_name(err));
        }
        if ((err = mdns_instance_name_set("PlantPal")) != ESP_OK) {
            ESP_LOGW(TAG, "mdns_instance_name_set failed: %s", esp_err_to_name(err));
        }
        if ((err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0)) != ESP_OK) {
            ESP_LOGW(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "mdns_init failed: %s", esp_err_to_name(err));
    }

    /* first scan on the task's first tick; empty list stays idle (AP only) */
    set_phase(s_n_saved ? PH_WAIT : PH_IDLE, 0);
    if (xTaskCreate(wifi_task, "wifi", 3584, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "wifi task create failed");
    }
}
