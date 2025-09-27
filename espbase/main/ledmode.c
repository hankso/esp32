/*
 * File: ledmode.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/11 0:34:11
 */

#include "ledmode.h"
#include "drivers.h"

#if defined(CONFIG_BASE_BOARD_S3ECAM) || defined(CONFIG_BASE_BOARD_S3XMINI)
#   if defined(CONFIG_BASE_USE_LED) && !defined(CONFIG_BASE_LED_MODE_RMT)
#       define CONFIG_BASE_LED_MODE_RMT
#       undef CONFIG_BASE_LED_MODE_GPIO
#       undef CONFIG_BASE_LED_MODE_LEDC
#   endif
#endif

#ifdef CONFIG_BASE_USE_LED
#   include "driver/ledc.h"
#   define SPEED_MODE  LEDC_LOW_SPEED_MODE
#   define LED_TMR     LEDC_TIMER_0
#   define LED_RES     LEDC_TIMER_13_BIT
#   define LED_CH      LEDC_CHANNEL_0
#   if __has_include("led_indicator.h")
#       define WITH_IND
#       include "led_indicator.h"
#   else
#       warning "Run `idf.py add-dependency espressif/led_indicator`"
#       include "led_strip.h"
#   endif
#endif

static UNUSED const char * TAG = "LEDMode";
static UNUSED led_blink_t state = LED_BLINK_RESET;

#ifndef CONFIG_BASE_LED_NUM
#   define CONFIG_BASE_LED_NUM  0
#endif

#if !defined(CONFIG_BASE_USE_LED)           // led disabled

static void *led_handle = NULL;
void led_initialize() {}

#elif defined(WITH_IND)                     // led enabled with component

#   ifdef CONFIG_BASE_LED_MODE_RMT
static const blink_step_t double_red_blink[] = {
    /*!< Set color to red by R:255 G:0 B:0 */
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 0},
    {LED_BLINK_HOLD, LED_STATE_ON, 500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_HOLD, LED_STATE_ON, 500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_STOP, 0, 0},
};

static const blink_step_t breath_white_slow_blink[] = {
    /*!< Set Color to white and brightness to zero by H:0 S:0 V:0 */
    {LED_BLINK_HSV, SET_HSV(0, 0, 0), 0},
    {LED_BLINK_BREATHE, LED_STATE_ON, 1000},
    {LED_BLINK_BREATHE, LED_STATE_OFF, 1000},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t breath_white_fast_blink[] = {
    /*!< Set Color to white and brightness to zero by H:0 S:0 V:0 */
    {LED_BLINK_HSV, SET_HSV(0, 0, 0), 0},
    {LED_BLINK_BREATHE, LED_STATE_ON, 500},
    {LED_BLINK_BREATHE, LED_STATE_OFF, 500},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t breath_blue_blink[] = {
    /*!< Set Color to blue and brightness to zero by H:240 S:255 V:0 */
    {LED_BLINK_HSV, SET_HSV(240, MAX_SATURATION, 0), 0},
    {LED_BLINK_BREATHE, LED_STATE_ON, 1000},
    {LED_BLINK_BREATHE, LED_STATE_OFF, 1000},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t color_hsv_ring_blink[] = {
    /*!< Set Color to RED */
    {LED_BLINK_HSV, SET_HSV(0, MAX_SATURATION, MAX_BRIGHTNESS), 0},
    {LED_BLINK_HSV_RING, SET_HSV(240, MAX_SATURATION, 127), 2000},
    {LED_BLINK_HSV_RING, SET_HSV(0, MAX_SATURATION, MAX_BRIGHTNESS), 2000},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t color_rgb_ring_blink[] = {
    /*!< Set Color to Green */
    {LED_BLINK_RGB, SET_RGB(0, 255, 0), 0},
    {LED_BLINK_RGB_RING, SET_RGB(255, 0, 255), 2000},
    {LED_BLINK_RGB_RING, SET_RGB(0, 255, 0), 2000},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t flowing_blink[] = {
    {LED_BLINK_HSV, SET_IHSV(MAX_INDEX, 0, MAX_SATURATION, MAX_BRIGHTNESS), 0},
    {LED_BLINK_HSV_RING, SET_IHSV(MAX_INDEX, MAX_HUE, MAX_SATURATION, MAX_BRIGHTNESS), 2000},
    {LED_BLINK_LOOP, 0, 0},
};

static const blink_step_t * LED_BLINK_LIST[] = {
    [LED_BLINK_WHITE_BREATHE_SLOW]  = breath_white_slow_blink,
    [LED_BLINK_WHITE_BREATHE_FAST]  = breath_white_fast_blink,
    [LED_BLINK_BLUE_BREATH]         = breath_blue_blink,
    [LED_BLINK_DOUBLE_RED]          = double_red_blink,
    [LED_BLINK_COLOR_HSV_RING]      = color_hsv_ring_blink,
    [LED_BLINK_COLOR_RGB_RING]      = color_rgb_ring_blink,
    [LED_BLINK_FLOWING]             = flowing_blink,
    [LED_BLINK_MAX]                 = NULL,
};
#   endif // CONFIG_BASE_LED_MODE_RMT

static led_indicator_handle_t led_handle;

void led_initialize() {
#   if defined(CONFIG_BASE_LED_MODE_GPIO)
    led_indicator_gpio_config_t gpio_conf = {
        .is_active_level_high = 1,
        .gpio_num = PIN_LED
    };
    led_indicator_config_t led_conf = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &gpio_conf,
    };
#   elif defined(CONFIG_BASE_LED_MODE_LEDC)
    led_indicator_ledc_config_t ledc_conf = {
        .is_active_level_high = 1,
        .timer_inited = false,
        .timer_num = LED_TMR,
        .gpio_num = PIN_LED,
        .channel = LED_CH,
    };
    led_indicator_config_t led_conf = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_conf,
    };
#   elif defined(CONFIG_BASE_LED_MODE_RMT)
    led_indicator_strips_config_t rmt_conf = {
        .led_strip_cfg = {
            .strip_gpio_num = PIN_LED,
            .max_leds = CONFIG_BASE_LED_NUM,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,
            .led_model = LED_MODEL_WS2812,
            .flags.invert_out = false,
        },
        .led_strip_driver = LED_STRIP_RMT,
        .led_strip_rmt_cfg = {
#       ifdef IDF_TARGET_V4
            .rmt_channel = 0,
#       else
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,  // 10MHz
#       endif
            .flags.with_dma = false,
        },
    };
    led_indicator_config_t led_conf = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = &rmt_conf,
        .blink_lists = LED_BLINK_LIST,
        .blink_list_num = LED_BLINK_MAX,
    };
#   else // CONFIG_LED_MODE_XXX
    ESP_LOGW(TAG, "disabled by CONFIG_LED_MODE_XXX");
    return;
#   endif // CONFIG_LED_MODE_XXX
    if (!( led_handle = led_indicator_create(&led_conf) )) {
        ESP_LOGW(TAG, "initialize indicator failed");
#   ifdef CONFIG_BASE_LED_MODE_GPIO
    } else {
        gpio_set_direction(PIN_LED, GPIO_MODE_INPUT_OUTPUT);
#   endif
    }
    led_set_blink(LED_BLINK_CONNECTED);
}

#else // WITH_IND                           // led enabled without component

typedef struct { uint8_t p, r, g, b; } led_color_t;
typedef struct {
    led_color_t *color;
    led_strip_handle_t strip;
    ledc_mode_t mode;
    ledc_timer_t timer;
    ledc_channel_t channel;
    float duty_scale;
} led_handle_t;

static led_handle_t * led_handle;

void led_initialize() {
    static led_handle_t local = {
        .mode = SPEED_MODE,
        .timer = LED_TMR,
        .channel = LED_CH,
    };
#   if defined(CONFIG_BASE_LED_MODE_GPIO)
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_INPUT_OUTPUT);
#   elif defined(CONFIG_BASE_LED_MODE_LEDC)
    ledc_timer_config_t timer_conf = {
        .speed_mode         = local.mode,
        .timer_num          = local.timer,
        .duty_resolution    = LED_RES,
        .freq_hz            = 5000,
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ledc_channel_config_t ch_conf = {
        .gpio_num           = PIN_LED,
        .speed_mode         = local.mode,
        .channel            = local.channel,
        .timer_sel          = local.timer,
        .duty               = 0,
    };
    if (ledc_timer_config(&timer_conf) || ledc_channel_config(&ch_conf)) {
        ESP_LOGE(TAG, "initialize ledc failed");
        return;
    }
    // mapping brightness 0-255 to duty by 13bit resolution
    local.duty_scale = ((1 << 13) - 1) / 255;
#   elif defined(CONFIG_BASE_LED_MODE_RMT)
    if (EALLOC(local.color, CONFIG_BASE_LED_NUM, sizeof(led_color_t))) {
        ESP_LOGE(TAG, "initialize led_color failed");
        return;
    }
    led_strip_config_t strip_conf = {
        .strip_gpio_num = PIN_LED,
        .max_leds = CONFIG_BASE_LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_conf = { .rmt_channel = 0 };
    if (led_strip_new_rmt_device(&strip_conf, &rmt_conf, &local.strip)) {
        ESP_LOGE(TAG, "initialize led_strip failed");
        TRYFREE(local.color);
        return;
    }
    led_strip_clear(local.strip);
#   else // CONFIG_BASE_LED_MODE_XXX
    ESP_LOGW(TAG, "disabled by CONFIG_LED_MODE_XXX");
    return;
#   endif // CONFIG_BASE_LED_MODE_XXX
    led_handle = &local;
    led_set_blink(0);
}

#endif // CONFIG_BASE_USE_LED

#if !defined(WITH_IND) && defined(CONFIG_BASE_LED_MODE_RMT)
static esp_err_t led_flush(int index, bool refresh) {
    uint8_t
        p = led_handle->color[index].p,
        r = led_handle->color[index].r * p / 255,
        g = led_handle->color[index].g * p / 255,
        b = led_handle->color[index].b * p / 255;
    esp_err_t err = led_strip_set_pixel(led_handle->strip, index, r, g, b);
    if (!err && refresh) err = led_strip_refresh(led_handle->strip);
    return err;
}
#endif

esp_err_t led_set_light(int index, uint8_t brightness) {
    if (!led_handle) return ESP_ERR_INVALID_STATE;
    if (index >= CONFIG_BASE_LED_NUM) return ESP_ERR_INVALID_ARG;
#if defined(WITH_IND)
#   if defined(CONFIG_BASE_LED_MODE_LEDC) || defined(CONFIG_BASE_LED_MODE_RMT)
    return led_indicator_set_brightness(
        led_handle, INSERT_INDEX(index < 0 ? MAX_INDEX : index, brightness));
#   else
    return led_indicator_set_on_off(led_handle, !!brightness);
#   endif
#elif defined(CONFIG_BASE_LED_MODE_GPIO)
    return gpio_set_level(PIN_LED, !!brightness);
#elif defined(CONFIG_BASE_LED_MODE_LEDC)
    uint32_t duty = brightness * led_handle->duty_scale;
    esp_err_t err = ledc_set_duty(led_handle->mode, led_handle->channel, duty);
    if (!err) err = ledc_update_duty(led_handle->mode, led_handle->channel);
    return err;
#elif defined(CONFIG_BASE_LED_MODE_RMT)
    esp_err_t err = ESP_OK;
    LOOPND(i, CONFIG_BASE_LED_NUM) {
        if (index >= 0 && index != i) continue;
        led_handle->color[i].p = brightness;
        if (( err = led_flush(i, index == i || !i) )) break;
    }
    return err;
#endif // CONFIG_BASE_LED_MODE_XXX
    return ESP_ERR_INVALID_STATE;
}

uint8_t led_get_light(int index) {
    if (!led_handle || index >= CONFIG_BASE_LED_NUM) return 0;
#if defined(WITH_IND)
    return led_indicator_get_brightness(led_handle);
#elif defined(CONFIG_BASE_LED_MODE_GPIO)
    return gpio_get_level(PIN_LED) ? 0xFF : 0;
#elif defined(CONFIG_BASE_LED_MODE_LEDC)
    uint32_t duty = ledc_get_duty(led_handle->mode, led_handle->channel);
    return (uint8_t)(duty / led_handle->duty_scale);
#elif defined(CONFIG_BASE_LED_MODE_RMT)
    return led_handle->color[MAX(0, index)].p;
#endif // CONFIG_LED_MODE_XXX
    return 0;
}

esp_err_t led_set_color(int index, uint32_t color) {
    if (!led_handle) return ESP_ERR_INVALID_STATE;
    if (index >= CONFIG_BASE_LED_NUM) return ESP_ERR_INVALID_ARG;
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
#if defined(WITH_IND)
#   if defined(CONFIG_BASE_LED_MODE_RMT)
    return led_indicator_set_rgb(
        led_handle, SET_IRGB(index < 0 ? MAX_INDEX : index, r, g, b));
#   endif
#elif defined(CONFIG_BASE_LED_MODE_GPIO)
    return led_set_light(index, color);
#elif defined(CONFIG_BASE_LED_MODE_LEDC)
    return led_set_light(index, (r + g + b) / 3);
#elif defined(CONFIG_BASE_LED_MODE_RMT)
    esp_err_t err = ESP_OK;
    LOOPND(i, CONFIG_BASE_LED_NUM) {
        if (index >= 0 && index != i) continue;
        led_handle->color[i].r = r;
        led_handle->color[i].g = g;
        led_handle->color[i].b = b;
        if (( err = led_flush(i, index == i || !i) )) break;
    }
    return err;
#endif // CONFIG_LED_MODE_XXX
    NOTUSED(r); NOTUSED(g); NOTUSED(b);
    return ESP_ERR_INVALID_STATE;
}

uint32_t led_get_color(int index) {
    if (!led_handle || index >= CONFIG_BASE_LED_NUM) return 0;
#if defined(WITH_IND)
    return led_indicator_get_rgb(led_handle);
#elif defined(CONFIG_BASE_LED_MODE_GPIO)
    return led_get_light(index) ? 0xFFFFFF : 0;
#elif defined(CONFIG_BASE_LED_MODE_LEDC)
    return led_get_light(index) * 0xFFFFFF / 0xFF;
#elif defined(CONFIG_BASE_LED_MODE_RMT)
    led_color_t *cptr = led_handle->color + MAX(0, index);
    return (cptr->r << 16) | (cptr->g << 8) | cptr->b;
#endif // CONFIG_LED_MODE_XXX
    return 0;
}

esp_err_t led_set_blink(led_blink_t blink) {
    if (!led_handle) return ESP_ERR_INVALID_STATE;
#ifdef WITH_IND
    esp_err_t err = ESP_OK;
    if (state > LED_BLINK_RESET) err = led_indicator_stop(led_handle, state);
    if (blink == LED_BLINK_RESET) {
        err = led_set_light(-1, 0);
    } else {
        err = led_indicator_start(led_handle, blink);
    }
    if (!err) state = blink;
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

led_blink_t led_get_blink() { return state; }
