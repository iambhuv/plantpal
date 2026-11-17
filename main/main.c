/* PlantPal — main application.
 *
 * Sensors: moisture on A0, DHT11 on D5.
 * Outputs: green/red/blue status LEDs, pump relay on D1 (dry-run until verified).
 * Input:   manual watering button on D0.
 * Network: saved Wi-Fi networks (http://plantpal.local) + fallback AP "PlantPal" (192.168.4.1).
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "plantpal.h"
#include "sensors.h"
#include "io.h"
#include "watering.h"
#include "state.h"
#include "wifi.h"
#include "settings.h"
#include "web.h"

#define LOOP_MS 50

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void app_main(void)
{
    printf("\n=== PlantPal: watering + dashboard ===\n");
    printf("pins: LEDs D2/D6/D7, button D0, pump D1, moisture A0, DHT11 D5\n");
    printf("pump: %s, free heap %u\n", PUMP_HW_VERIFIED ? "ENABLED" : "dry-run (relay not driven)",
           (unsigned)esp_get_free_heap_size());
    printf("policy: threshold %d%%, water %d ms, cooldown %d ms\n",
           MOISTURE_THRESHOLD_PCT, WATER_DURATION_MS, COOLDOWN_MS);

    io_init();
    sensors_init();
    state_init();

    wifi_start();   /* nvs_flash_init first; settings_init below needs it */
    settings_init();
    printf("wifi: started, free heap %u\n", (unsigned)esp_get_free_heap_size());
    web_start();

    uint32_t last_moisture = 0, last_dht = 0;
    bool have_moisture = false, have_dht = false, tried_dht = false;
    uint16_t raw = 0;
    uint8_t pct = 0;
    float temp_c = 0.0f;
    uint8_t humidity = 0;

    for (;;) {
        uint32_t now = now_ms();

        if (!have_moisture || (now - last_moisture) >= MOISTURE_SAMPLE_MS) {
            raw = sensors_moisture_raw();
            pct = sensors_moisture_pct(raw);
            last_moisture = now;
            have_moisture = true;
            printf("moisture: raw=%u  %u%%\n", raw, pct);
        }

        /* retry a missing sensor at the normal cadence too: each read masks IRQs */
        if (!tried_dht || (now - last_dht) >= DHT_SAMPLE_MS) {
            tried_dht = true;
            float t;
            uint8_t h;
            have_dht = sensors_read_dht(&t, &h);
            last_dht = now;
            if (have_dht) {
                temp_c = t;
                humidity = h;
                printf("climate: %.1f C, %u %%\n", temp_c, humidity);
            } else {
                printf("climate: DHT11 read failed\n");
            }
        }

        if (io_button_pressed()) {
            printf("button: manual watering requested\n");
            watering_request_manual(WATER_SRC_BUTTON);
        }

        bool pump_on = watering_update(pct);
        leds_set_status(pct >= watering_get_threshold(), pump_on);

        sensor_snapshot_t snap = {
            .moisture_raw = raw,
            .moisture_pct = pct,
            .temp_c = temp_c,
            .humidity = humidity,
            .have_dht = have_dht,
        };
        state_publish_sensors(&snap);

        vTaskDelay(LOOP_MS / portTICK_PERIOD_MS);
    }
}
