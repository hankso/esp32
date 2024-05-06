/*
 * File: ledmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/11 0:31:05
 */

#pragma once

#include "globals.h"

#if defined(CONFIG_BASE_LED_INDICATOR) && !__has_include("led_indicator.h")
#   warning "Run `idf.py add-dependency espressif/led_indicator`"
#   undef CONFIG_BASE_LED_INDICATOR
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_BLINK_RESET = -1,
#ifdef CONFIG_BASE_LED_MODE_RMT
    LED_BLINK_WHITE_BREATHE_SLOW,
    LED_BLINK_WHITE_BREATHE_FAST,
    LED_BLINK_BLUE_BREATH,
    LED_BLINK_DOUBLE_RED,
    LED_BLINK_TRIPLE_GREEN,
    LED_BLINK_COLOR_HSV_RING,
    LED_BLINK_COLOR_RGB_RING,
    LED_BLINK_FLOWING,
#else
    LED_BLINK_FACTORY_RESET,
    LED_BLINK_UPDATING,
    LED_BLINK_CONNECTED,
    LED_BLINK_PROVISIONED,
    LED_BLINK_CONNECTING,
    LED_BLINK_RECONNECTING,
    LED_BLINK_PROVISIONING,
#endif
    LED_BLINK_MAX,
} led_blink_t;

void led_initialize();

esp_err_t   led_set_light(int index, uint8_t brightness);
uint8_t     led_get_light(int index);
esp_err_t   led_set_color(int index, uint32_t color);
uint32_t    led_get_color(int index);
esp_err_t   led_set_blink(led_blink_t blink);
led_blink_t led_get_blink();

#ifdef __cplusplus
}
#endif
