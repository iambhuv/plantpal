/* PlantPal — sensors: moisture (A0) and DHT11 (D5). */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"

#include "plantpal.h"
#include "sensors.h"

#include "driver_dht11.h"
#include "driver_dht11_interface.h"

#define MOISTURE_AVG_SAMPLES 16

/* The ESP8266 SAR ADC is shared with the Wi-Fi RF front-end, so a reading can
 * briefly stick to the rail (1023) while the radio is active — indistinguishable
 * from a genuinely dry probe. A dry probe also drifts up gradually, so a sudden
 * jump to 1023 from a wet value is treated as a glitch and ignored unless it
 * persists for a few consecutive reads. */
#define MOISTURE_GLITCH_RAW        1023
#define MOISTURE_GLITCH_WET_MAX    700    /* below this we consider "recently wet" */
#define MOISTURE_GLITCH_MAX_STREAK 2

static dht11_handle_t s_dht;
static bool s_dht_ok;
static uint16_t s_last_raw;
static bool s_have_last;
static uint8_t s_glitch_streak;

static void dht_link(void)
{
    DRIVER_DHT11_LINK_INIT(&s_dht, dht11_handle_t);
    DRIVER_DHT11_LINK_BUS_INIT(&s_dht, dht11_interface_init);
    DRIVER_DHT11_LINK_BUS_DEINIT(&s_dht, dht11_interface_deinit);
    DRIVER_DHT11_LINK_BUS_READ(&s_dht, dht11_interface_read);
    DRIVER_DHT11_LINK_BUS_WRITE(&s_dht, dht11_interface_write);
    DRIVER_DHT11_LINK_DELAY_MS(&s_dht, dht11_interface_delay_ms);
    DRIVER_DHT11_LINK_DELAY_US(&s_dht, dht11_interface_delay_us);
    DRIVER_DHT11_LINK_ENABLE_IRQ(&s_dht, dht11_interface_enable_irq);
    DRIVER_DHT11_LINK_DISABLE_IRQ(&s_dht, dht11_interface_disable_irq);
    DRIVER_DHT11_LINK_DEBUG_PRINT(&s_dht, dht11_interface_debug_print);
}

void sensors_init(void)
{
    adc_config_t cfg = {
        .mode = ADC_READ_TOUT_MODE,
        .clk_div = 8,
    };
    if (adc_init(&cfg) != ESP_OK) {
        printf("sensors: adc_init failed\n");
    }

    dht_link();
    s_dht_ok = (dht11_init(&s_dht) == 0);
    printf("sensors: dht11 %s\n", s_dht_ok ? "ok" : "init failed");
}

uint16_t sensors_moisture_raw(void)
{
    uint32_t sum = 0;
    uint32_t n = 0;

    for (int i = 0; i < MOISTURE_AVG_SAMPLES; i++) {
        uint16_t v = 0;
        if (adc_read(&v) == ESP_OK) {
            sum += v;
            n++;
        }
    }
    uint16_t raw = (n > 0) ? (uint16_t)(sum / n) : 0;

    if (s_have_last && raw == MOISTURE_GLITCH_RAW &&
        s_last_raw < MOISTURE_GLITCH_WET_MAX) {
        if (s_glitch_streak < MOISTURE_GLITCH_MAX_STREAK) {
            s_glitch_streak++;
            return s_last_raw;   /* transient rail glitch, keep last good value */
        }
    } else {
        s_glitch_streak = 0;
    }

    s_last_raw = raw;
    s_have_last = true;
    s_glitch_streak = 0;
    return raw;
}

uint8_t sensors_moisture_pct(uint16_t raw)
{
    if (raw >= MOISTURE_RAW_DRY) {
        return 0;
    }
    if (raw <= MOISTURE_RAW_WET) {
        return 100;
    }
    return (uint8_t)(((uint32_t)(MOISTURE_RAW_DRY - raw) * 100) /
                     (MOISTURE_RAW_DRY - MOISTURE_RAW_WET));
}

bool sensors_read_dht(float *temp_c, uint8_t *humidity_pct)
{
    uint16_t temp_raw = 0;
    uint16_t hum_raw = 0;

    if (!s_dht_ok) {
        return false;
    }
    return dht11_read_temperature_humidity(&s_dht, &temp_raw, temp_c,
                                           &hum_raw, humidity_pct) == 0;
}
