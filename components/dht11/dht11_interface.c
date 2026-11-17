/* PlantPal — ESP8266 RTOS SDK HAL for libdriver/dht11
 *
 * Implements the 9 interface functions the driver expects. The one-wire bus
 * is a single GPIO driven as:
 *   write(0) -> push-pull output LOW   (start pulse / bit start)
 *   write(1) -> released, high-Z; the enabled pull-up raises the line
 *   read()   -> read the actual line level (valid in either state)
 *
 * The driver calls disable_irq()/enable_irq() in balanced pairs, so they map
 * to vPortEnterCritical()/vPortExitCritical() (nest-safe function form — the
 * portDISABLE_INTERRUPTS macro needs a local cpu_sr and cannot be wrapped).
 *
 * Note: GPIO16 (D0) is NOT usable here, it has no pull-up; the DHT11 is on
 * D5 / GPIO14 as wired in the handoff.
 */
#include <stdarg.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/gpio.h"
#include "rom/ets_sys.h"

#include "driver_dht11_interface.h"

#define DHT11_BUS_GPIO  GPIO_NUM_14

uint8_t dht11_interface_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << DHT11_BUS_GPIO),
        .mode         = GPIO_MODE_INPUT,          /* idle: released, pull-up high */
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&io) != ESP_OK) {
        return 1;
    }
    return 0;
}

uint8_t dht11_interface_deinit(void)
{
    if (gpio_set_direction(DHT11_BUS_GPIO, GPIO_MODE_INPUT) != ESP_OK) {
        return 1;
    }
    return 0;
}

uint8_t dht11_interface_read(uint8_t *value)
{
    if (value == NULL) {
        return 1;
    }
    /* ROM inline, no function call into flash — safe with IRQs disabled */
    *value = (uint8_t)GPIO_INPUT_GET(DHT11_BUS_GPIO);
    return 0;
}

uint8_t dht11_interface_write(uint8_t value)
{
    if (value != 0) {
        /* release the bus; the pull-up pulls it high */
        if (gpio_set_direction(DHT11_BUS_GPIO, GPIO_MODE_INPUT) != ESP_OK) {
            return 1;
        }
    } else {
        if (gpio_set_direction(DHT11_BUS_GPIO, GPIO_MODE_OUTPUT) != ESP_OK) {
            return 1;
        }
        if (gpio_set_level(DHT11_BUS_GPIO, 0) != ESP_OK) {
            return 1;
        }
    }
    return 0;
}

void dht11_interface_delay_ms(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

void dht11_interface_delay_us(uint32_t us)
{
    ets_delay_us(us);
}

void dht11_interface_enable_irq(void)
{
    vPortExitCritical();
}

void dht11_interface_disable_irq(void)
{
    vPortEnterCritical();
}

void dht11_interface_debug_print(const char *const fmt, ...)
{
    char buf[192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* ets_printf is the ROM console — no newlib locks, safe inside a
     * critical section (the driver prints on some failure paths with
     * interrupts still disabled). */
    ets_printf("%s", buf);
}
