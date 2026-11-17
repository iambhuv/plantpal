/* PlantPal — LEDs, button, pump. */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "plantpal.h"
#include "io.h"

static bool s_btn_was_pressed;

void io_init(void)
{
    gpio_config_t leds = {
        .pin_bit_mask = (1ULL << PIN_LED_GREEN) | (1ULL << PIN_LED_RED) |
                        (1ULL << PIN_LED_BLUE),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&leds);
    gpio_set_level(PIN_LED_GREEN, LED_OFF);
    gpio_set_level(PIN_LED_RED, LED_OFF);
    gpio_set_level(PIN_LED_BLUE, LED_OFF);

    /* GPIO16 has no internal pull-up — external 10k to 3V3 is required. */
    gpio_config_t btn = {
        .pin_bit_mask = (1ULL << PIN_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn);

    gpio_config_t pump = {
        .pin_bit_mask = (1ULL << PIN_PUMP),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* keep the relay off at boot */
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pump);
    gpio_set_level(PIN_PUMP, 0);
}

void leds_set_status(bool moisture_ok, bool pump_on)
{
    /* blue wins: pump activity */
    gpio_set_level(PIN_LED_BLUE,  pump_on ? LED_ON : LED_OFF);
    gpio_set_level(PIN_LED_GREEN, (!pump_on && moisture_ok) ? LED_ON : LED_OFF);
    gpio_set_level(PIN_LED_RED,   (!pump_on && !moisture_ok) ? LED_ON : LED_OFF);
}

bool io_button_pressed(void)
{
    bool pressed = (gpio_get_level(PIN_BUTTON) == 0);

    if (pressed == s_btn_was_pressed) {
        return false;
    }

    vTaskDelay(25 / portTICK_PERIOD_MS);          /* debounce */
    if ((gpio_get_level(PIN_BUTTON) == 0) != pressed) {
        return false;                              /* bounced back */
    }

    s_btn_was_pressed = pressed;
    return pressed;                                /* event on the press edge */
}

void io_pump_set(bool on)
{
#if PUMP_HW_VERIFIED
    gpio_set_level(PIN_PUMP, on ? 1 : 0);
#else
    (void)on;   /* dry-run: logic runs, relay stays off */
#endif
}
