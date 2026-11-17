/* PlantPal — HTTP server (esp_http_server): embedded panel + JSON API.
 *
 * Endpoints:
 *   GET  /                  panel (gzipped HTML, ETag; If-None-Match -> 304)
 *   GET  /api/status        status JSON (sensors, policy, net, recent events)
 *   GET  /api/events        SSE of the same status JSON: on change (heap and
 *                           uptime_s excluded) or every 10 s
 *   POST /api/water         one manual watering cycle -> {"ok":true}
 *   POST /api/settings      any of {auto,threshold,run_s,wait_s,ap_enabled}
 *                           -> status JSON (values clamped, saved)
 *   GET  /api/wifi          saved networks [{ssid,state,rssi}]
 *   POST /api/wifi          {"ssid","pass"?} add/edit -> list (409 when full)
 *   POST /api/wifi/delete   {"ssid"} -> list (404 if not saved)
 *   POST /api/restart       {"ok":true}, reboots ~500 ms later
 * POSTs need Content-Type: application/json (else 415), body <= 512 B.
 * Errors are {"error":"..."}; a POST with any invalid field applies nothing.
 *
 * Every handler and the SSE broadcast run on the single httpd task, so the
 * static buffers below are never used concurrently.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "io.h"
#include "json_depth.h"
#include "policy.h"
#include "settings.h"
#include "state.h"
#include "watering.h"
#include "web.h"
#include "wifi.h"

static const char *TAG = "web";

#define STATUS_JSON_SIZE 1536
#define BODY_MAX 512
#define JSON_DEPTH_MAX 4
#define SSE_REFRESH_MS 10000
#define WIFI_MAX 5
#define SSE_MAX_CLIENTS 4
#define SSE_FRAME_SIZE (STATUS_JSON_SIZE + 32)

typedef struct {
    bool used;
    bool closing;
    bool initial;
    httpd_handle_t server;
    int socket;
    uint32_t version;
    TickType_t last_sent;
    TickType_t pending_since;
    size_t length;
    size_t sent;
    char buffer[SSE_FRAME_SIZE];
} sse_client_t;

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

static sse_client_t s_clients[SSE_MAX_CLIENTS];
static uint32_t s_status_hash;          /* status_hash() at last change */
static uint32_t s_status_version;
static char s_json[STATUS_JSON_SIZE];    /* scratch for every response */
static char s_body[BODY_MAX + 1];
static event_t s_events[EVENTLOG_CAP];
static wifi_net_t s_nets[WIFI_MAX];
static char s_etag[11];                  /* "xxxxxxxx" incl. quotes */

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

/* msg is always a literal from this file, never user input. */
static esp_err_t send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", msg);
    httpd_resp_set_status(req, status);
    return send_json(req, buf);
}

static esp_err_t send_oom(httpd_req_t *req)
{
    return send_error(req, "500 Internal Server Error", "out of memory");
}

/* Print and free root into s_json; send it, or 500 if anything failed. */
static esp_err_t send_cjson(httpd_req_t *req, cJSON *root)
{
    bool ok = root && cJSON_PrintPreallocated(root, s_json, sizeof(s_json), false);
    cJSON_Delete(root);
    return ok ? send_json(req, s_json) : send_oom(req);
}

static uint32_t fnv1a(uint32_t h, const void *data, size_t n)
{
    for (const uint8_t *p = data; n--; p++) {
        h = (h ^ *p) * 16777619u;
    }
    return h;
}

#define FNV_SEED 2166136261u

/* ---- panel ---- */

static esp_err_t index_html(httpd_req_t *req)
{
    char inm[sizeof(s_etag)];

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "ETag", s_etag);
    if (httpd_req_get_hdr_value_len(req, "If-None-Match") == strlen(s_etag) &&
        httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK &&
        strcmp(inm, s_etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, NULL, 0);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    esp_err_t err = httpd_resp_send(req, (const char *)index_html_gz_start,
                                    index_html_gz_end - index_html_gz_start);
    ESP_LOGI(TAG, "GET / -> %s, heap %u", esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
    return err;
}

/* ---- status ---- */

#define ADD(call) ok = ok && (call) != NULL

static int temp_tenths(float c)
{
    return (int)(c * 10.0f + (c < 0 ? -0.5f : 0.5f));
}

/* Status JSON into buf; returns its length, 0 on allocation failure. If it
 * does not fit (many long SSIDs in the log), the oldest events are dropped. */
static size_t status_json(char *buf, size_t size)
{
    sensor_snapshot_t s = state_sensors();
    wifi_status_t w = wifi_status();
    size_t n_ev = state_events(s_events, EVENTLOG_CAP);
    double t = temp_tenths(s.temp_c) / 10.0;

    cJSON *root = cJSON_CreateObject();
    cJSON *net = cJSON_CreateObject();
    cJSON *events = cJSON_CreateArray();
    bool ok = root && net && events;

    ADD(cJSON_AddNumberToObject(root, "moisture_raw", s.moisture_raw));
    ADD(cJSON_AddNumberToObject(root, "moisture_pct", s.moisture_pct));
    ADD(cJSON_AddNumberToObject(root, "temp_c", t));
    ADD(cJSON_AddNumberToObject(root, "humidity", s.humidity));
    ADD(cJSON_AddBoolToObject(root, "dht_ok", s.have_dht));
    ADD(cJSON_AddBoolToObject(root, "pump_on", watering_get_pump()));
    ADD(cJSON_AddBoolToObject(root, "auto", watering_get_auto()));
    ADD(cJSON_AddNumberToObject(root, "threshold", watering_get_threshold()));
    ADD(cJSON_AddNumberToObject(root, "run_s", watering_get_run_s()));
    ADD(cJSON_AddNumberToObject(root, "wait_s", watering_get_wait_s()));
    ADD(cJSON_AddNumberToObject(root, "last_watered_s", watering_last_watered_s()));
    ADD(cJSON_AddNumberToObject(root, "uptime_s", state_uptime_s()));
    ADD(cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size()));
    ADD(cJSON_AddBoolToObject(root, "ap_enabled", settings_ap_enabled()));

    ADD(cJSON_AddBoolToObject(net, "sta", w.sta_up));
    ADD(cJSON_AddStringToObject(net, "ssid", w.ssid));
    ADD(cJSON_AddStringToObject(net, "ip", w.ip));
    ADD(cJSON_AddNumberToObject(net, "rssi", w.rssi));
    ADD(cJSON_AddBoolToObject(net, "ap", w.ap_up));

    for (size_t i = 0; ok && i < n_ev; i++) {
        cJSON *e = cJSON_CreateObject();
        ok = e != NULL;
        if (ok) {
            cJSON_AddItemToArray(events, e);
        }
        ADD(cJSON_AddNumberToObject(e, "t", s_events[i].t_s));
        ADD(cJSON_AddStringToObject(e, "k", eventlog_kind_name(s_events[i].kind)));
        ADD(cJSON_AddNumberToObject(e, "n", s_events[i].n));
        ADD(cJSON_AddStringToObject(e, "s", s_events[i].s));
    }

    if (ok) {
        /* CS = constant key: no allocation, so these cannot fail */
        cJSON_AddItemToObjectCS(root, "net", net);
        net = NULL;
        cJSON_AddItemToObjectCS(root, "events", events);
        while (!cJSON_PrintPreallocated(root, buf, (int)size, false)) {
            int count = cJSON_GetArraySize(events);
            if (count == 0) {
                ok = false;
                break;
            }
            cJSON_DeleteItemFromArray(events, count - 1);
        }
    } else {
        cJSON_Delete(events);
    }
    cJSON_Delete(net);
    cJSON_Delete(root);
    return ok ? strlen(buf) : 0;
}

static esp_err_t send_status(httpd_req_t *req)
{
    return status_json(s_json, sizeof(s_json)) ? send_json(req, s_json) : send_oom(req);
}

static esp_err_t api_status(httpd_req_t *req)
{
    return send_status(req);
}

/* Hash of what the panel needs pushed: the status fields minus heap, uptime_s
 * and the ticking last_watered_s (only never/ever counts; pump_on marks a run
 * ending). Cheap, no cJSON. A field missed here only waits for the 10 s
 * refresh; keep it in step with status_json() anyway. */
static uint32_t status_hash(void)
{
    sensor_snapshot_t s = state_sensors();
    wifi_status_t w = wifi_status();
    size_t n = state_events(s_events, EVENTLOG_CAP);
    int32_t v[] = {
        s.moisture_raw, s.moisture_pct, temp_tenths(s.temp_c), s.humidity, s.have_dht,
        watering_get_pump(), watering_get_auto(), watering_get_threshold(),
        watering_get_run_s(), watering_get_wait_s(), watering_last_watered_s() >= 0,
        settings_ap_enabled(), w.sta_up, w.rssi, w.ap_up, (int32_t)n,
    };
    uint32_t h = fnv1a(FNV_SEED, v, sizeof(v));

    h = fnv1a(h, w.ssid, strlen(w.ssid));
    h = fnv1a(h, w.ip, strlen(w.ip));
    for (size_t i = 0; i < n; i++) {
        int32_t e[] = { (int32_t)s_events[i].t_s, s_events[i].kind, (int32_t)s_events[i].n };
        h = fnv1a(h, e, sizeof(e));
        h = fnv1a(h, s_events[i].s, strlen(s_events[i].s));
    }
    return h;
}

/* ---- SSE ---- */

static void events_release(void *context)
{
    sse_client_t *client = context;
    memset(client, 0, sizeof(*client));
}

static int events_recv(httpd_handle_t server, int socket, char *buf, size_t size, int flags)
{
    (void)server;
    (void)socket;
    (void)buf;
    (void)size;
    (void)flags;
    return HTTPD_SOCK_ERR_FAIL;
}

static esp_err_t api_events(httpd_req_t *req)
{
    if (req->content_len || req->sess_ctx) {
        return ESP_FAIL;
    }

    sse_client_t *client = NULL;
    for (size_t index = 0; index < SSE_MAX_CLIENTS; index++) {
        if (!s_clients[index].used) {
            client = &s_clients[index];
            break;
        }
    }

    if (!client) {
        return send_error(req, "503 Service Unavailable", "too many event streams");
    }

    int socket = httpd_req_to_sockfd(req);
    if (socket < 0) {
        return ESP_FAIL;
    }

    client->used = true;
    client->initial = true;
    client->server = req->handle;
    client->socket = socket;
    client->last_sent = xTaskGetTickCount();
    req->sess_ctx = client;
    req->free_ctx = events_release;

    if (httpd_resp_set_type(req, "text/event-stream") != ESP_OK ||
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache") != ESP_OK ||
        httpd_sess_set_recv_override(req->handle, socket, events_recv) != ESP_OK) {
        return ESP_FAIL;
    }

    static const char retry[] = "retry: 2000\n\n";
    return httpd_resp_send_chunk(req, retry, sizeof(retry) - 1);
}

static void events_close(sse_client_t *client)
{
    client->closing = true;
    shutdown(client->socket, SHUT_RDWR);
}

static void events_flush(sse_client_t *client, TickType_t now)
{
    if ((TickType_t)(now - client->pending_since) >= pdMS_TO_TICKS(5000)) {
        events_close(client);
        return;
    }

    int count = send(client->socket, client->buffer + client->sent,
                     client->length - client->sent, MSG_DONTWAIT);
    if (count > 0) {
        httpd_sess_update_lru_counter(client->server, client->socket);
        client->sent += (size_t)count;
        if (client->sent == client->length) {
            client->length = 0;
            client->sent = 0;
            client->last_sent = now;
        }
    } else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        events_close(client);
    }
}

/* Frame a client when the status changed, on connect, or every 10 s (that
 * refresh is also the heartbeat). The JSON is built at most once per tick and
 * only when some client needs it. */
static void events_broadcast(void *context)
{
    uint32_t h = status_hash();
    if (h != s_status_hash) {
        s_status_hash = h;
        s_status_version++;
    }

    TickType_t now = xTaskGetTickCount();
    bool built = false;
    size_t len = 0;

    for (size_t index = 0; index < SSE_MAX_CLIENTS; index++) {
        sse_client_t *client = &s_clients[index];
        if (!client->used || client->closing) {
            continue;
        }

        if (!client->length &&
            (client->initial || client->version != s_status_version ||
             (TickType_t)(now - client->last_sent) >= pdMS_TO_TICKS(SSE_REFRESH_MS))) {
            if (!built) {
                built = true;
                len = status_json(s_json, sizeof(s_json));
            }
            if (!len) {
                continue;  /* out of memory: retry next tick */
            }

            /* chunk = "data: " + status + "\n\n" */
            int length = snprintf(client->buffer, sizeof(client->buffer),
                                  "%x\r\ndata: %s\n\n\r\n", (unsigned)len + 8, s_json);
            if (length <= 0 || (size_t)length >= sizeof(client->buffer)) {
                events_close(client);
                continue;
            }
            client->initial = false;
            client->version = s_status_version;
            client->length = (size_t)length;
            client->pending_since = now;
        }

        if (client->length) {
            events_flush(client, now);
        }
    }

    xTaskNotifyGive((TaskHandle_t)context);
}

static void events_task(void *context)
{
    httpd_handle_t server = context;
    TaskHandle_t task = xTaskGetCurrentTaskHandle();

    for (;;) {
        if (httpd_queue_work(server, events_broadcast, task) == ESP_OK) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/* ---- POST helpers ---- */

/* Every POST: require Content-Type application/json and read the whole body
 * (<= 512 B) into s_body. On rejection sends the error and returns false with
 * *res = ESP_FAIL, which closes the socket instead of letting httpd drain an
 * unread body (its drain loop spins forever if the peer closes). */
static bool recv_body(httpd_req_t *req, esp_err_t *res)
{
    char type[64];
    size_t tlen = httpd_req_get_hdr_value_len(req, "Content-Type");
    *res = ESP_FAIL;

    if (tlen == 0 || tlen >= sizeof(type) ||
        httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strncasecmp(type, "application/json", 16) != 0) {
        send_error(req, "415 Unsupported Media Type", "Content-Type must be application/json");
        return false;
    }
    if (req->content_len > BODY_MAX) {
        send_error(req, HTTPD_400, "body too large");
        return false;
    }

    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, s_body + got, req->content_len - got);
        if (r <= 0) {
            return false;  /* peer gone or stalled past recv_wait_timeout */
        }
        got += (size_t)r;
    }
    s_body[got] = '\0';
    *res = ESP_OK;
    return true;
}

/* recv_body + parse a JSON object. NULL = rejected, handler returns *res. */
static cJSON *read_body(httpd_req_t *req, esp_err_t *res)
{
    if (!recv_body(req, res)) {
        return NULL;
    }

    const char *err;
    cJSON *root = NULL;
    if (req->content_len == 0) {
        err = "missing body";
    } else if (!json_depth_ok(s_body, req->content_len, JSON_DEPTH_MAX)) {
        err = "JSON nested too deeply";
    } else {
        root = cJSON_ParseWithOpts(s_body, NULL, true);
        err = "invalid JSON";  /* also what cJSON reports on OOM */
    }
    memset(s_body, 0, sizeof(s_body));  /* may hold a wifi password */

    if (root && cJSON_IsObject(root)) {
        return root;
    }
    cJSON_Delete(root);
    *res = send_error(req, HTTPD_400, err);
    return NULL;
}

static bool is_int(const cJSON *j)
{
    return cJSON_IsNumber(j) && j->valuedouble == (double)j->valueint;
}

/* ---- actions ---- */

static esp_err_t api_water(httpd_req_t *req)
{
    esp_err_t res;
    if (!recv_body(req, &res)) {
        return res;
    }
    watering_request_manual(WATER_SRC_PANEL);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t api_settings(httpd_req_t *req)
{
    esp_err_t res;
    cJSON *body = read_body(req, &res);
    if (!body) {
        return res;
    }

    const cJSON *a = cJSON_GetObjectItemCaseSensitive(body, "auto");
    const cJSON *th = cJSON_GetObjectItemCaseSensitive(body, "threshold");
    const cJSON *run = cJSON_GetObjectItemCaseSensitive(body, "run_s");
    const cJSON *wait = cJSON_GetObjectItemCaseSensitive(body, "wait_s");
    const cJSON *ap = cJSON_GetObjectItemCaseSensitive(body, "ap_enabled");

    if ((a && !cJSON_IsBool(a)) || (ap && !cJSON_IsBool(ap))) {
        cJSON_Delete(body);
        return send_error(req, HTTPD_400, "auto and ap_enabled must be booleans");
    }
    if ((th && !is_int(th)) || (run && !is_int(run)) || (wait && !is_int(wait))) {
        cJSON_Delete(body);
        return send_error(req, HTTPD_400, "threshold, run_s and wait_s must be integers");
    }

    /* Validated: apply. Clamp in long (valueint is saturated by cJSON) so the
     * u8/u16 setters never see a truncated value. */
    bool changed = false;
    if (a && cJSON_IsTrue(a) != watering_get_auto()) {
        watering_set_auto(cJSON_IsTrue(a));
        settings_save_auto(watering_get_auto());
        changed = true;
    }
    if (th) {
        long v = policy_clamp(th->valueint, 0, 100);
        if (v != watering_get_threshold()) {
            watering_set_threshold((uint8_t)v);
            settings_save_threshold(watering_get_threshold());
            changed = true;
        }
    }
    if (run) {
        long v = policy_clamp(run->valueint, RUN_S_MIN, RUN_S_MAX);
        if (v != watering_get_run_s()) {
            watering_set_run_s((uint8_t)v);
            settings_save_run_s(watering_get_run_s());
            changed = true;
        }
    }
    if (wait) {
        long v = policy_clamp(wait->valueint, WAIT_S_MIN, WAIT_S_MAX);
        if (v != watering_get_wait_s()) {
            watering_set_wait_s((uint16_t)v);
            settings_save_wait_s(watering_get_wait_s());
            changed = true;
        }
    }
    if (ap) {
        bool on = cJSON_IsTrue(ap);
        changed |= on != settings_ap_enabled();
        settings_save_ap_enabled(on);  /* so the status below shows it now */
        wifi_set_ap_enabled(on);       /* always forwarded; wifi task applies it */
    }
    cJSON_Delete(body);

    if (changed) {
        state_log(EV_SETTINGS, 0, "");
    }
    return send_status(req);
}

static const char *net_state_name(wifi_net_state_t st)
{
    switch (st) {
    case WIFI_NET_CONNECTED: return "connected";
    case WIFI_NET_IN_RANGE:  return "in_range";
    case WIFI_NET_BAD_PASS:  return "bad_pass";
    default:                 return "away";
    }
}

static esp_err_t send_wifi_list(httpd_req_t *req)
{
    size_t n = wifi_networks(s_nets, WIFI_MAX);
    cJSON *root = cJSON_CreateArray();
    bool ok = root != NULL;

    for (size_t i = 0; ok && i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        ok = e != NULL;
        if (ok) {
            cJSON_AddItemToArray(root, e);
        }
        ADD(cJSON_AddStringToObject(e, "ssid", s_nets[i].ssid));
        ADD(cJSON_AddStringToObject(e, "state", net_state_name(s_nets[i].state)));
        ADD(cJSON_AddNumberToObject(e, "rssi", s_nets[i].rssi));
    }
    if (!ok) {
        cJSON_Delete(root);
        return send_oom(req);
    }
    return send_cjson(req, root);
}

static esp_err_t api_wifi_get(httpd_req_t *req)
{
    return send_wifi_list(req);
}

static esp_err_t send_wifi_err(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_ERR_TIMEOUT) {
        return send_error(req, "503 Service Unavailable", "wifi busy, try again");
    }
    return send_error(req, "500 Internal Server Error", "wifi error");
}

static esp_err_t api_wifi_save(httpd_req_t *req)
{
    esp_err_t res;
    cJSON *body = read_body(req, &res);
    if (!body) {
        return res;
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(body, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(body, "pass");
    if (!cJSON_IsString(ssid) || (pass && !cJSON_IsNull(pass) && !cJSON_IsString(pass))) {
        cJSON_Delete(body);
        return send_error(req, HTTPD_400, "ssid must be a string, pass a string if given");
    }

    esp_err_t err = wifi_save_network(ssid->valuestring,
                                      cJSON_IsString(pass) ? pass->valuestring : NULL);
    cJSON_Delete(body);

    switch (err) {
    case ESP_OK:
        return send_wifi_list(req);
    case ESP_ERR_INVALID_ARG:
        return send_error(req, HTTPD_400, "ssid 1-32 chars, password empty or 8-64 chars");
    case ESP_ERR_NO_MEM:
        return send_error(req, "409 Conflict", "5 networks saved, remove one first");
    default:
        return send_wifi_err(req, err);
    }
}

static esp_err_t api_wifi_delete(httpd_req_t *req)
{
    esp_err_t res;
    cJSON *body = read_body(req, &res);
    if (!body) {
        return res;
    }

    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(body, "ssid");
    if (!cJSON_IsString(ssid)) {
        cJSON_Delete(body);
        return send_error(req, HTTPD_400, "ssid must be a string");
    }

    esp_err_t err = wifi_delete_network(ssid->valuestring);
    cJSON_Delete(body);

    switch (err) {
    case ESP_OK:
        return send_wifi_list(req);
    case ESP_ERR_NOT_FOUND:
        return send_error(req, "404 Not Found", "network not saved");
    default:
        return send_wifi_err(req, err);
    }
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    io_pump_set(false);
    esp_restart();
}

static esp_err_t api_restart(httpd_req_t *req)
{
    esp_err_t res;
    if (!recv_body(req, &res)) {
        return res;
    }
    res = send_json(req, "{\"ok\":true}");
    if (xTaskCreate(restart_task, "restart", 1536, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        io_pump_set(false);
        esp_restart();  /* no heap for a task: restart now, reply likely sent */
    }
    return res;
}

/* ---- start ---- */

void web_start(void)
{
    /* FNV-1a 32 over the embedded panel; it only changes with a reflash. */
    uint32_t h = fnv1a(FNV_SEED, index_html_gz_start,
                       index_html_gz_end - index_html_gz_start);
    snprintf(s_etag, sizeof(s_etag), "\"%08x\"", (unsigned)h);

    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 5120;  /* cJSON + %g printing; JSON buffers are static */
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;
    cfg.send_wait_timeout = 5;   /* SSE pushes use MSG_DONTWAIT, unaffected */

    httpd_uri_t uris[] = {
        { .uri = "/",                .method = HTTP_GET,  .handler = index_html },
        { .uri = "/api/status",      .method = HTTP_GET,  .handler = api_status },
        { .uri = "/api/events",      .method = HTTP_GET,  .handler = api_events },
        { .uri = "/api/water",       .method = HTTP_POST, .handler = api_water },
        { .uri = "/api/settings",    .method = HTTP_POST, .handler = api_settings },
        { .uri = "/api/wifi",        .method = HTTP_GET,  .handler = api_wifi_get },
        { .uri = "/api/wifi",        .method = HTTP_POST, .handler = api_wifi_save },
        { .uri = "/api/wifi/delete", .method = HTTP_POST, .handler = api_wifi_delete },
        { .uri = "/api/restart",     .method = HTTP_POST, .handler = api_restart },
    };

    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        if (httpd_register_uri_handler(server, &uris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "handler registration failed: %s", uris[i].uri);
            httpd_stop(server);
            return;
        }
    }

    if (xTaskCreate(events_task, "events", 2048, server, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "event task creation failed");
        httpd_stop(server);
        return;
    }

    ESP_LOGI(TAG, "panel up at http://192.168.4.1 (etag %s)", s_etag);
}
