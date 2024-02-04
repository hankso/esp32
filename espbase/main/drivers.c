/* 
 * File: drivers.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:54:28
 */

#include "sdkconfig.h"
#include "drivers.h"

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_vfs_dev.h"
#include "esp_adc_cal.h"
#include "esp_task_wdt.h"
#include "esp_intr_alloc.h"
#include "soc/soc.h"
#include "sys/param.h"
#include "driver/adc.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if defined(CONFIG_I2C_SCREEN) && __has_include("u8g2_esp32_hal.h")
#   include "u8g2.h"
#   include "u8g2_esp32_hal.h"
#   define WITH_U8G2
#endif
#if defined(CONFIG_VLX_SENSOR) && __has_include("vl53l0x.h")
#    include "vl53l0x.h"
#    define WITH_VLX
#endif
#include "iot_knob.h"
#include "iot_button.h"

static const char *TAG = "Driver";

// LED Controller and Indicator

#ifdef CONFIG_LED_INDICATOR

#   include "led_indicator.h"
static led_indicator_handle_t led_handle;

static void led_initialize() {
#   if defined(CONFIG_LED_MODE_GPIO)
    led_indicator_gpio_config_t gpio_conf = {
        .is_active_level_high = 1,
        .gpio_num = PIN_LED
    };
    led_indicator_config_t led_conf = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &gpio_conf,
    };
#   elif defined(CONFIG_LED_MODE_LEDC)
    led_indicator_ledc_config_t ledc_conf = {
        .is_active_level_high = 1,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_0,
        .gpio_num = PIN_LED,
        .channel = LEDC_CHANNEL_0,
    };
    led_indicator_config_t led_conf = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_conf,
    };
#   elif defined(CONFIG_LED_MODE_RMT)
    led_indicator_strips_config_t rmt_conf = {
        .led_strip_cfg = {
            .strip_gpio_num = PIN_LED,
            .max_leds = CONFIG_LED_NUM,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB,
            .led_model = LED_MODEL_WS2812,
            .flags.invert_out = false,
        },
        .led_strip_driver = LED_STRIP_RMT,
        .led_strip_rmt_cfg = {
#       if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
            .rmt_channel = 0,
#       else
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,  // 10MHz
            .flags.with_dma = false,
#       endif
        },
    };
    led_indicator_config_t led_conf = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = &rmt_conf,
    };
#   else // CONFIG_LED_MODE_XXX
    ESP_LOGI(TAG, "LED: disabled by CONFIG_LED_MODE_XXX");
    return;
#   endif // CONFIG_LED_MODE_XXX
    // #include "led_indicator_blink_default.h"
    // led_conf.blinnk_lists = default_led_indicator_blink_lists;
    // led_conf.blinnk_lists_num = DEFAULT_BLINK_LIST_NUM;
    if (!( led_handle = led_indicator_create(&led_conf) )) {
        ESP_LOGW(TAG, "LED: initialize indicator failed");
#ifdef CONFIG_LED_MODE_GPIO
    } else {
        gpio_set_direction(PIN_LED, GPIO_MODE_INPUT_OUTPUT); // gpio_get_level
#endif
    }
}

#else // CONFIG_LED_INDICATOR

#   include "led_strip.h"
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

static void led_initialize() {
    static led_handle_t local = {
        .mode = LEDC_LOW_SPEED_MODE,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
    };
#   if defined(CONFIG_LED_MODE_GPIO)
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_INPUT_OUTPUT);
#   elif defined(CONFIG_LED_MODE_LEDC)
    ledc_timer_config_t timer_conf = {
        .speed_mode         = local.mode,
        .timer_num          = local.timer,
        .duty_resolution    = LEDC_TIMER_13_BIT,
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
        ESP_LOGE(TAG, "LED: initialize ledc failed");
        return;
    }
    // mapping brightness 0-255 to duty by 13bit resolution
    local.duty_scale = ((1 << 13) - 1) / 255;
#   elif defined(CONFIG_LED_MODE_RMT)
    if (!( local.color = calloc(CONFIG_LED_NUM, sizeof(led_color_t)) )) {
        ESP_LOGE(TAG, "LED: initialize led_color failed");
        return;
    }
    led_strip_config_t strip_conf = {
        .strip_gpio_num = PIN_LED,
        .max_leds = CONFIG_LED_NUM,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_conf = { .rmt_channel = 0 };
    if (led_strip_new_rmt_device(&strip_conf, &rmt_conf, &local.strip)) {
        ESP_LOGE(TAG, "LED: initialize led_strip failed");
        TRYFREE(local.color);
        return;
    }
    led_strip_clear(local.strip);
#   else // CONFIG_LED_MODE_XXX
    ESP_LOGI(TAG, "LED: disabled by CONFIG_LED_MODE_XXX");
    return;
#   endif // CONFIG_LED_MODE_XXX
    led_handle = &local;
}

#endif // CONFIG_LED_INDICATOR

#if !defined(CONFIG_LED_INDICATOR) && defined(CONFIG_LED_MODE_RMT)
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
    if (index >= CONFIG_LED_NUM) return ESP_ERR_INVALID_ARG;
#if defined(CONFIG_LED_INDICATOR)
#   if defined(CONFIG_LED_MODE_LEDC) || defined(CONFIG_LED_MODE_RMT)
    return led_indicator_set_brightness(
        led_handle, INSERT_INDEX(index < 0 ? MAX_INDEX : index, brightness));
#   else
    return led_indicator_set_on_off(led_handle, !!brightness);
#   endif
#elif defined(CONFIG_LED_MODE_GPIO)
    return gpio_set_level(PIN_LED, !!brightness);
#elif defined(CONFIG_LED_MODE_LEDC)
    uint32_t duty = brightness * led_handle->duty_scale;
    esp_err_t err = ledc_set_duty(led_handle->mode, led_handle->channel, duty);
    if (!err) err = ledc_update_duty(led_handle->mode, led_handle->channel);
    return err;
#elif defined(CONFIG_LED_MODE_RMT)
    esp_err_t err = ESP_OK;
    LOOPND(i, CONFIG_LED_NUM) {
        if (index >= 0 && index != i) continue;
        led_handle->color[i].p = brightness;
        if (( err = led_flush(i, index == i || !i) )) break;
    }
    return err;
#endif // CONFIG_LED_MODE_XXX
    return ESP_ERR_INVALID_STATE;
}

uint8_t led_get_light(int index) {
    if (!led_handle || index >= CONFIG_LED_NUM) return 0;
#if defined(CONFIG_LED_INDICATOR)
    return led_indicator_get_brightness(led_handle);
#elif defined(CONFIG_LED_MODE_GPIO)
    return gpio_get_level(PIN_LED) ? 0xFF : 0;
#elif defined(CONFIG_LED_MODE_LEDC)
    uint32_t duty = ledc_get_duty(led_handle->mode, led_handle->channel);
    return (uint8_t)(duty / led_handle->duty_scale);
#elif defined(CONFIG_LED_MODE_RMT)
    return led_handle->color[index < 0 ? 0 : index].p;
#endif // CONFIG_LED_MODE_XXX
    return 0;
}

esp_err_t led_set_color(int index, uint32_t color) {
    if (!led_handle) return ESP_ERR_INVALID_STATE;
    if (index >= CONFIG_LED_NUM) return ESP_ERR_INVALID_ARG;
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
#if defined(CONFIG_LED_INDICATOR)
#   if defined(CONFIG_LED_MODE_RMT)
    return led_indicator_set_rgb(
        led_handle, SET_IRGB(index < 0 ? MAX_INDEX : index, r, g, b));
#   endif
#elif defined(CONFIG_LED_MODE_GPIO)
    return led_set_light(index, color);
#elif defined(CONFIG_LED_MODE_LEDC)
    return led_set_light(index, (r + g + b) / 3);
#elif defined(CONFIG_LED_MODE_RMT)
    esp_err_t err = ESP_OK;
    LOOPND(i, CONFIG_LED_NUM) {
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
    if (!led_handle || index >= CONFIG_LED_NUM) return 0;
#if defined(CONFIG_LED_INDICATOR)
    return led_indicator_get_rgb(led_handle);
#elif defined(CONFIG_LED_MODE_GPIO)
    return led_get_light(index) ? 0xFFFFFF : 0;
#elif defined(CONFIG_LED_MODE_LEDC)
    return led_get_light(index) * 0xFFFFFF / 0xFF;
#elif defined(CONFIG_LED_MODE_RMT)
    led_color_t *cptr = led_handle->color + (index < 0 ? 0 : index);
    return (cptr->r << 16) | (cptr->g << 8) | cptr->b;
#endif // CONFIG_LED_MODE_XXX
    return 0;
}

esp_err_t led_set_blink(int blink_type) {
    static int last = -1;
    if (!led_handle) return ESP_ERR_INVALID_STATE;
#ifdef CONFIG_LED_INDICATOR
    esp_err_t err = ESP_OK;
    if (blink_type == last)
        return err;
    if (blink_type >= 0)
        err = led_indicator_start(led_handle, blink_type);
    if (!err) {
        if (last >= 0)
            led_indicator_stop(led_handle, last);
        last = blink_type;
    }
    return err;
#endif
    return ESP_ERR_INVALID_STATE;
}

// ADC analog in

#ifdef CONFIG_ADC_INPUT
static esp_adc_cal_characteristics_t adc_chars;
static const adc_unit_t adc_unit = ADC_UNIT_1;              // ADC 1
static const adc_atten_t adc_atten = ADC_ATTEN_DB_11;       // 0.15-2.45V
static const adc_bits_width_t adc_width = ADC_WIDTH_BIT_12; // Dmax=4095
static const adc1_channel_t adc_chan = ADC1_CHANNEL_6;      // Pin 34
#endif

static void adc_initialize() {
#ifdef CONFIG_ADC_INPUT
#if CONFIG_IDF_TARGET_ESP32
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF)) {
        ESP_LOGI(TAG, "ADC: eFuse VRef not supported");
    } else {
        ESP_LOGD(TAG, "ADC: eFuse VRef supported");
    }
#endif // CONFIG_IDF_TARGET_ESP32
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP)) {
        ESP_LOGI(TAG, "ADC: eFuse Two Point not supported");
    } else {
        ESP_LOGD(TAG, "ADC: eFuse Two Point supported");
    }
#else
    ESP_LOGE(TAG, "ADC: Unknown ESP chip target");
    return;
#endif // CONFIG_IDF_TARGET_ESP32
    adc1_config_width(adc_width);
    adc1_config_channel_atten(adc_chan, adc_atten);
    memset(&adc_chars, 0, sizeof(adc_chars));
    esp_adc_cal_value_t vtype = esp_adc_cal_characterize(
        adc_unit, adc_atten, adc_width, 1100, &adc_chars);
    if (vtype == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "ADC: characterized using Two Point Value");
    } else if (vtype == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "ADC: characterized using eFuse VRef");
    } else {
        ESP_LOGI(TAG, "ADC: characterized using Default VRef");
    }
#endif // CONFIG_ADC_INPUT
}

uint32_t adc_read() {
#ifndef CONFIG_ADC_INPUT
    return 0;
#elif defined(ADC_MULTISAMPLING)
    uint32_t raw;
    LOOPN(i, ADC_MULTISAMPLING) {
        raw += adc1_get_raw(adc_chan);
    }
    raw /= ADC_MULTISAMPLING;
    return esp_adc_cal_raw_to_voltage(raw, &adc_chars);
#else
    return esp_adc_cal_raw_to_voltage(adc1_get_raw(adc_chan), &adc_chars);
#endif
}

// PWM by LEDC

#define SPEED_MODE  LEDC_LOW_SPEED_MODE
#define RES_SERVO   LEDC_TIMER_10_BIT
#define RES_BUZZER  LEDC_TIMER_10_BIT

static void pwm_initialize() {
#ifdef CONFIG_PWM_SERVO
    ledc_timer_config_t servo_conf = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = LEDC_TIMER_1,
        .duty_resolution    = RES_SERVO,
        .freq_hz            = 50, // 20ms
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&servo_conf));
    ledc_channel_config_t channel0_conf = {
        .gpio_num           = PIN_SVOH,
        .speed_mode         = SPEED_MODE,
        .channel            = LEDC_CHANNEL_1,
        .timer_sel          = LEDC_TIMER_1,
        .hpoint             = 0,
        .duty               = 0
    };
    ledc_channel_config_t channel1_conf = {
        .gpio_num           = PIN_SVOV,
        .speed_mode         = SPEED_MODE,
        .channel            = LEDC_CHANNEL_2,
        .timer_sel          = LEDC_TIMER_1,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel0_conf));
    ESP_ERROR_CHECK(ledc_channel_config(&channel1_conf));
#endif
#ifdef CONFIG_PWM_BUZZER
    ledc_timer_config_t buzzer_conf = {
        .speed_mode         = SPEED_MODE,
        .timer_num          = LEDC_TIMER_2,
        .duty_resolution    = RES_BUZZER,
        .freq_hz            = 1,
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&buzzer_conf));
    ledc_channel_config_t channel2_conf = {
        .gpio_num           = PIN_BUZZ,
        .speed_mode         = SPEED_MODE,
        .channel            = LEDC_CHANNEL_3,
        .timer_sel          = LEDC_TIMER_2,
        .hpoint             = 0,
        .duty               = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel2_conf));
#endif
}

static esp_err_t pwm_set_duty(int channel, int duty) {
    esp_err_t err = ledc_set_duty(SPEED_MODE, channel, duty);
    if (!err) err = ledc_update_duty(SPEED_MODE, channel);
    return err;
}

#ifdef CONFIG_PWM_SERVO
// mapping 0-180 deg to 0.5-2.5 ms
static float servo_offset = 0.5 / 20 * ((1 << RES_SERVO) - 1);
static float servo_scale  = 2.0 / 20 * ((1 << RES_SERVO) - 1) / 180;

esp_err_t pwm_set_degree(int hdeg, int vdeg) {
    esp_err_t err = ESP_OK;
    if (!err && hdeg >= 0) {
        // calibration: convert 0-180 to real 14-180 degree
        hdeg = 166 * hdeg / 180 + 14;
        err = pwm_set_duty(
            LEDC_CHANNEL_1,
            MIN(hdeg, 180) * servo_scale + servo_offset
        );
    }
    if (!err && vdeg >= 0) {
        err = pwm_set_duty(
            LEDC_CHANNEL_2,
            MIN(vdeg, 160) * servo_scale + servo_offset
        );
    }
    return err;
}

esp_err_t pwm_get_degree(int *hdeg, int *vdeg) {
    int hduty = ledc_get_duty(SPEED_MODE, LEDC_CHANNEL_1),
        vduty = ledc_get_duty(SPEED_MODE, LEDC_CHANNEL_2);
    *hdeg = (int)((hduty - servo_offset) / servo_scale);
    *vdeg = (int)((vduty - servo_offset) / servo_scale);
    return ESP_OK;
}

#else
esp_err_t pwm_set_degree(int h, int v) {
    return ESP_ERR_NOT_FOUND; NOTUSED(h); NOTUSED(v);
}
esp_err_t pwm_get_degree(int *h, int *v) {
    return ESP_ERR_NOT_FOUND; NOTUSED(h); NOTUSED(v);
}
#endif

#ifdef CONFIG_PWM_BUZZER
// mapping 0-100 percent to 0%-50% of duty
static float buzzer_scale = ((1 << RES_BUZZER) - 1) / 200;

esp_err_t pwm_set_tone(uint32_t freq, int pcent) {
    esp_err_t err = ESP_OK;
    if (!err && pcent >= 0) {
        err = pwm_set_duty(LEDC_CHANNEL_3, pcent * buzzer_scale);
    }
    if (!err && freq <= 20000) {
        err = ledc_set_freq(SPEED_MODE, LEDC_TIMER_2, freq);
    }
    return err;
}

esp_err_t pwm_get_tone(uint32_t *freq, int *pcent) {
    *freq = ledc_get_freq(SPEED_MODE, LEDC_TIMER_2);
    *pcent = (int)(ledc_get_duty(SPEED_MODE, LEDC_CHANNEL_3) / buzzer_scale);
    return ESP_OK;
}

#else
esp_err_t pwm_set_tone(uint32_t f, int p) {
    return ESP_ERR_NOT_FOUND; NOTUSED(f); NOTUSED(p);
}
esp_err_t pwm_get_tone(uint32_t *f, int *p) {
    return ESP_ERR_NOT_FOUND; NOTUSED(f); NOTUSED(p);
}
#endif

// I2C GPIO Expander

static esp_err_t i2c_master_config(int bus, int sda, int scl, int speed) {
    i2c_config_t master_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE
    };
    master_conf.master.clk_speed = speed;
    return i2c_param_config(bus, &master_conf);
}

static void i2c_initialize() {
    ESP_ERROR_CHECK( i2c_driver_install(NUM_I2C, I2C_MODE_MASTER, 0, 0, 0) );
    ESP_ERROR_CHECK( i2c_master_config(NUM_I2C, PIN_SDA0, PIN_SCL0, 50000) );
}

esp_err_t smbus_probe(int bus, uint8_t addr) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t smbus_wregs(int bus, uint8_t addr, uint8_t reg, uint8_t * val, size_t len) {
    // SMBus Write protocol:
    //      S | (ADDR | W) | ACK | REG | ACK | (DATA | ACK) * n | P
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, val, len, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t smbus_rregs(int bus, uint8_t addr, uint8_t reg, uint8_t *val, size_t len) {
    // SMBus Read protocol:
    //      S | (ADDR | W) | ACK | REG | ACK |
    //      S | (ADDR | R) | ACK | (DATA | A) * n - 1 | (DATA | N) | P
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr << 1 | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, addr << 1 | I2C_MASTER_READ, true);
    if (len > 1)
        i2c_master_read(cmd, val, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, val + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t smbus_write_byte(int bus, uint8_t addr, uint8_t reg, uint8_t val) {
    return smbus_wregs(bus, addr, reg, &val, 1);
}

esp_err_t smbus_read_byte(int bus, uint8_t addr, uint8_t reg, uint8_t *val) {
    return smbus_rregs(bus, addr, reg, val, 1);
}

esp_err_t smbus_write_word(int bus, uint8_t addr, uint8_t reg, uint16_t val) {
    uint8_t buf[2] = { val >> 8, val & 0xFF };
    return smbus_wregs(bus, addr, reg, buf, 2);
}

esp_err_t smbus_read_word(int bus, uint8_t addr, uint8_t reg, uint16_t *val) {
    uint8_t buf[2] = { 0, 0 };
    esp_err_t err = smbus_rregs(bus, addr, reg, buf, 2);
    if (!err) *val = buf[0] << 8 | buf[1];
    return err; // FIXME: check_endian and skip this tedious conversion
}

esp_err_t smbus_dump(int bus, uint8_t addr, uint8_t start, uint8_t end) {
    int length = 16;
    size_t len = end - start;
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) return ESP_ERR_NO_MEM;
    esp_err_t err = smbus_rregs(bus, addr, start, buf, len);
    if (err) return err;
    for (uint8_t reg = start; reg <= end; reg++) {
        if (reg == start) {
            printf("I2C %d-%02X register table\n", bus, addr);
            printf("ADDR:");
            LOOPN(i, length) {
                printf(" %02X", i);
            }
            printf("\n%04X:%*s", reg - (reg % length), 3 * (reg % length), "");
        } else if (reg % length == 0) {
            printf("\n%04X:", reg);
        }
        printf(" %02X", buf[reg - start]);
    }
    putchar('\n');
    return err;
}

void i2c_detect(int bus) {
    LOOPN(i, 0x10) {
        if (!i) printf("  ");
        printf(" %02X", i);
    }
    LOOPN(addr, 0x7F) {
        if (addr % 0x10 == 0)
            printf("\n%02X", addr);
        if (!addr) {
            printf("   ");
            continue;
        }
        switch (smbus_probe(bus, addr)) {
        case ESP_OK:
            printf(" %02X", addr); break;
        case ESP_ERR_TIMEOUT:
            printf(" UU"); break;
        default:
            printf(" --");
        }
    }
}

static esp_err_t UNUSED i2c_trans(int bus, uint8_t addr,
                                        uint8_t rw, uint8_t *data, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | rw, true);
    if (rw == I2C_MASTER_WRITE) {
        if (size > 1) {
            i2c_master_write(cmd, data, size, true);
        } else {
            i2c_master_write_byte(cmd, *data, true);
        }
    } else if (rw == I2C_MASTER_READ) {
        if (size > 1) {
            i2c_master_read(cmd, data, size - 1, I2C_MASTER_ACK);
            i2c_master_read_byte(cmd, data + size - 1, I2C_MASTER_LAST_NACK);
        } else {
            i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
        }
    }
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(bus, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

#ifdef CONFIG_I2C_GPIOEXP
static uint8_t i2c_pin_data[3] = { 0, 0, 0 };
static uint8_t i2c_pin_addr[3] = { 0b0100000, 0b0100001, 0b0100010 };
#endif

esp_err_t i2c_gpio_set_level(i2c_pin_num_t pin_num, bool level) {
#ifdef CONFIG_I2C_GPIOEXP
    if (!PIN_IS_I2CEXP(pin_num)) return ESP_ERR_INVALID_ARG;
    int pin = pin_num - PIN_I2C_MIN;
    uint8_t idx = pin >> 3, mask = BIT(pin & 0x7), *datp = i2c_pin_data + idx;
    *datp = level ? (*datp | mask) : (*datp & ~mask);
    return i2c_trans(NUM_I2C, i2c_pin_addr[idx], I2C_MASTER_WRITE, datp, 1);
#else
    return ESP_ERR_NOT_FOUND;
#endif // CONFIG_I2C_GPIOEXP
}

esp_err_t i2c_gpio_get_level(i2c_pin_num_t pin_num, bool * level, bool sync) {
#ifdef CONFIG_I2C_GPIOEXP
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!PIN_IS_I2CEXP(pin_num)) return err;
    int pin = pin_num - PIN_I2C_MIN;
    uint8_t idx = pin >> 3, mask = BIT(pin & 0x7), *datp = i2c_pin_data + idx;
    if (sync)
        err = i2c_trans(NUM_I2C, i2c_pin_addr[idx], I2C_MASTER_READ, datp, 1);
    if (!err)
        *level = *datp & mask;
    return err;
#else
    return ESP_ERR_NOT_FOUND;
#endif // CONFIG_I2C_GPIOEXP
}


// I2C Distance Measurement


#ifdef WITH_VLX
static vl53l0x_t *vlx;
#endif

static void vlx_initialize() {
    uint8_t addr = 0x29;
    if (smbus_probe(NUM_I2C, addr)) return;
#ifdef WITH_VLX
    vlx = vl53l0x_config(NUM_I2C, PIN_SCL0, PIN_SDA0, -1, addr, 0);
    const char *err = vl53l0x_init(vlx);
    if (err) {
        ESP_LOGE(TAG, "VLX: initialize VL53L0X failed: %s", err);
        vl53l0x_end(vlx);
        vlx = NULL;
    }
#else
    ESP_LOGE(TAG, "VLX: sensor is not supported");
#endif // WITH_VLX
}

uint16_t vlx_probe() {
#ifdef WITH_VLX
    TickType_t tick_start = xTaskGetTickCount();
    uint16_t result_mm = vl53l0x_readRangeSingleMillimeters(vlx);
    int took_ms = ((int)xTaskGetTickCount() - tick_start) * portTICK_PERIOD_MS;
    if (result_mm != (uint16_t)-1) {
        printf("VLX: range %u mm took %d ms", result_mm, took_ms);
    } else {
        printf("VLX: failed to measure range");
    }
    return result_mm;
#else
    return 0;
#endif // WITH_VLX
}


// I2C OLED Screen


#ifdef WITH_U8G2

static u8g2_t scn;
static bool scn_init = false;

static void scn_initialize() {
    // MACROs defined in u8g2_esp32_hal.h
    int bus = I2C_MASTER_NUM, speed = I2C_MASTER_FREQ_HZ, addr = 0x3C;
    if (i2c_driver_install(bus, I2C_MODE_MASTER, 0, 0, 0)) return;
    scn_init = i2c_master_config(bus, PIN_SDA1, PIN_SCL1, speed) == ESP_OK;
    scn_init = scn_init && smbus_probe(bus, addr) == ESP_OK;
    if (!scn_init) return;
    i2c_driver_delete(bus);

    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.sda = PIN_SDA1;
    u8g2_esp32_hal.scl = PIN_SCL1;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &scn, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&scn.u8x8, addr << 1);
    u8g2_SetFont(&scn, u8g2_font_ncenB08_tr);
    u8g2_InitDisplay(&scn);
    u8g2_SetPowerSave(&scn, 0);
}

void scn_progbar(uint8_t percent) {
    if (!scn_init) return;
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", percent);
    u8g2_ClearBuffer(&scn);
    u8g2_DrawFrame(&scn, 0, 20, 128, 6);
    u8g2_DrawBox(&scn, 0, 20, 128 * percent / 100, 6);
    u8g2_DrawStr(&scn, (128 - u8g2_GetStrWidth(&scn, buf)) / 2, 28 + 8, buf);
    u8g2_SendBuffer(&scn);
}

#else

static void scn_initialize() { ESP_LOGE(TAG, "Screen is not supported"); }
void scn_progbar(uint8_t percent) { ; }

#endif // WITH_U8G2


// I2C Ambient Light and Temperature Sensor
// 7bit I2C address of the GY39 is 0x5B.

esp_err_t gy39_measure(int bus, gy39_data_t *d) {
    esp_err_t err;
    uint8_t a[0x0D];
    if (( err = smbus_rregs(bus, 0x5B, 0x00, a, sizeof(a)) )) return err;
    d->brightness = 1e-2 * ((a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3]);
    d->temperature = 1e-2 * ((a[4] << 8) | a[5]);
    d->atmosphere = 1e-2 * ((a[6] << 24) | (a[7] << 16) | (a[8] << 8) | a[9]);
    d->humidity = 1e-2 * ((a[10] << 8) | a[11]);
    d->altitude = (a[12] << 8) | a[13];
    return err;
}

// I2C Ambient Light Sensor
// 7bit I2C address of the OPT3001 is configurable by ADDR PIN.
// Basic address is 0b010001XX where `XX` are:
//      ADDR -> GND: 0b00
//      ADDR -> VDD: 0b01
//      ADDR -> SDA: 0b10
//      ADDR -> SCL: 0b11

#ifdef CONFIG_ALS_TRACK
static uint8_t i2c_als_addr[4] = {
    0b01000100, 0b01000101,         // east, west
    0b01000110, 0b01000111          // south, north
};
#endif

static void als_initialize() {
#ifdef CONFIG_ALS_TRACK
    esp_err_t err;
    uint8_t addr;
    uint16_t buf[2];
    LOOPN(i, 4) {
        if (smbus_probe(NUM_I2C, addr = i2c_als_addr[i]))
            continue;
        if (
            ( err = smbus_read_word(NUM_I2C, addr, 0x7E, buf + 0) ) ||
            ( err = smbus_read_word(NUM_I2C, addr, 0x7F, buf + 1) )
        ) {
            ESP_LOGE(TAG, "ALS: read %d failed: %s", i, esp_err_to_name(err));
            continue;
        }
        ESP_LOGI(TAG, "ALS: found %c%c %04X at I2C %d-%02X",
                buf[0] >> 8, buf[0] & 0xFF, buf[1], NUM_I2C, addr);
        smbus_write_word(NUM_I2C, addr, 0x01, 0xC610); // continuous mode
        // TODO: configure Low-Limit and Hight-Limit and Interrupt GPIO
    }
#endif // CONFIG_ALS_TRACK
}

float als_brightness(int idx) {
#ifdef CONFIG_ALS_TRACK
    if (idx < 0 || idx > 3) {
        ESP_LOGE(TAG, "ALS: invalid chip index %d", idx);
        return 0;
    }
    uint16_t val;
    esp_err_t err;
    if (( err = smbus_read_word(NUM_I2C, i2c_als_addr[idx], 0x00, &val) )) {
        ESP_LOGW(TAG, "ALS: read %d failed: %s", idx, esp_err_to_name(err));
        return 0;
    }
    // Equation 3 at Page 20 of OPT3001 datasheet:
    //   lux = 0.01 * 2^E[3:0] * R[11:0]
    return 0.01 * (1 << (val >> 12)) * (val & 0xFFF);
#else
    return 0;
#endif // CONFIG_ALS_TRACK
}

esp_err_t als_tracking(als_track_t idx, int *hdeg, int *vdeg) {
#ifdef CONFIG_ALS_TRACK
    esp_err_t err;
    float bmax = 0, bmin = 1e10, btmp[4];
    switch (idx) {
    case ALS_TRACK_0: case ALS_TRACK_1:     // maximize brightness
    case ALS_TRACK_2: case ALS_TRACK_3:
        for (int i = 0, v = 0; v < 90; i++, v += 6) {
            for (int h = 0; h < 180; h += 5) {
                int htmp = i % 2 ? (180 - h) : h; // S line scanning
                if (( err = pwm_set_degree(htmp, v) )) return err;
                msleep(50);
                btmp[0] = als_brightness(idx);
                ESP_LOGD(TAG, "ALS: H %3d V %3d %8.2f lux\n", htmp, v, btmp[0]);
                if (btmp[0] > bmax) {
                    bmax = btmp[0];
                    *hdeg = htmp;
                    *vdeg = v;
                }
            }
        }
        break;
    case ALS_TRACK_H:           // minimize difference of east and west
        for (int h = 0; h < 180; h += 15) {
            if (( err = pwm_set_degree(h, -1) )) return err;
            msleep(200);
            if (( btmp[0] = als_brightness(0) ) > bmax) bmax = btmp[0];
            if (( btmp[1] = als_brightness(1) ) > bmax) bmax = btmp[1];
            if (( btmp[2] = ABSDIFF(btmp[0], btmp[1]) ) < bmin) {
                bmin = btmp[2];
                *hdeg = h;
            }
        }
        break;
    case ALS_TRACK_V:           // minimize difference of north and south
        for (int v = 0; v < 90; v += 9) {
            if (( err = pwm_set_degree(-1, v) )) return err;
            msleep(200);
            if (( btmp[0] = als_brightness(2) ) > bmax) bmax = btmp[0];
            if (( btmp[1] = als_brightness(3) ) > bmax) bmax = btmp[1];
            if (( btmp[2] = ABSDIFF(btmp[0], btmp[1]) ) < bmin) {
                bmin = btmp[2];
                *vdeg = v;
            }
        }
        break;
    case ALS_TRACK_A:
        puts("TODO: PID Light Tracking"); break;
    default: return ESP_ERR_INVALID_ARG;
    }
    if (bmax != 0 || bmin != 1e10) {
        return pwm_set_degree(*hdeg, *vdeg);
    }
#endif // CONFIG_ALS_TRACK
    return ESP_ERR_NOT_FOUND;
}

// SPI GPIO Expander
// If transmitted data is 32bits or less, spi_transaction_t can use tx_data.
// Here we have no more than 4 chips, thus SPI_TRANS_USE_TXDATA.
#ifdef CONFIG_SPI_GPIOEXP
static spi_device_handle_t spi_pin_hdlr;
static spi_transaction_t spi_pin_trans;
static uint8_t spi_pin_data[2] = { 0, 0 };
#endif

static void spi_initialize() {
#ifdef CONFIG_SPI_GPIOEXP
    spi_bus_config_t hspi_busconf = {
        .mosi_io_num = PIN_HMOSI,
        .miso_io_num = PIN_HMISO,
        .sclk_io_num = PIN_HSCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .intr_flags = 0
    };
    spi_device_interface_config_t devconf = {
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
        .mode = 0b10,                       // CPOL = 1, CPHA = 0
        .duty_cycle_pos = 128,              // 128/255 = 50% (Tlow = Thigh)
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .clock_speed_hz = 5 * 1000 * 1000,  // 5MHz
        .input_delay_ns = 0,
        .spics_io_num = PIN_HCS1,
        .flags = 0,
        .queue_size = 1,                    // only one transaction allowed
        .pre_cb = NULL,
        .post_cb = NULL
    };
    esp_err_t err = spi_bus_initialize(HSPI_HOST, &hspi_busconf, 1);
    assert((!err || err == ESP_ERR_INVALID_STATE) && "SPI init failed");
    ESP_ERROR_CHECK( spi_bus_add_device(HSPI_HOST, &devconf, &spi_pin_hdlr) );
    spi_pin_trans.length = LEN(spi_pin_data) * 8; // in bits;
    spi_pin_trans.tx_buffer = spi_pin_data;
#endif // CONFIG_SPI_GPIOEXP
}

esp_err_t spi_gpio_set_level(spi_pin_num_t pin_num, bool level) {
#ifdef CONFIG_SPI_GPIOEXP
    if (!PIN_IS_SPIEXP(pin_num)) return ESP_ERR_INVALID_ARG;
    int pin = pin_num - PIN_SPI_MIN;
    uint8_t idx = pin >> 3, mask = BIT(pin & 0x7), *datp = spi_pin_data + idx;
    *datp = level ? (*datp | mask) : (*datp & ~mask);
    return spi_device_polling_transmit(spi_pin_hdlr, &spi_pin_trans);
#else
    return ESP_ERR_NOT_FOUND;
#endif // CONFIG_SPI_GPIOEXP
}

esp_err_t spi_gpio_get_level(spi_pin_num_t pin_num, bool * level, bool sync) {
#ifdef CONFIG_SPI_GPIOEXP
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!PIN_IS_SPIEXP(pin_num)) return err;
    int pin = pin_num - PIN_SPI_MIN;
    uint8_t idx = pin >> 3, mask = BIT(pin & 0x7);
    if (sync)
        err = spi_device_polling_transmit(spi_pin_hdlr, &spi_pin_trans);
    if (!err)
        *level = spi_pin_data[idx] & mask;
    return err;
#else
    return ESP_ERR_NOT_FOUND;
#endif // CONFIG_SPI_GPIOEXP
}

// GPIO Interrupt

static button_handle_t btn;
#ifdef CONFIG_KNOB_INPUT
static knob_handle_t knob;
#endif

static void gpio_cb_single_click(void *arg, void *data) {
    ESP_LOGI(TAG, "BTN: %d single click", PIN_BTN);
}

static void gpio_cb_double_click(void *arg, void *data) {
    ESP_LOGI(TAG, "BTN: %d double click", PIN_BTN);
}

static void gpio_cb_long_press(void *arg, void *data) {
    ESP_LOGI(TAG, "BTN: %d long press", PIN_BTN);
}

static void UNUSED gpio_cb_left_rotate(void *arg, void *data) {
    ESP_LOGI(TAG, "Knob: left rotate %d",
            iot_knob_get_count_value((button_handle_t)arg));
}

static void UNUSED gpio_cb_right_rotate(void *arg, void *data) {
    ESP_LOGI(TAG, "Knob: right rotate %d",
            iot_knob_get_count_value((button_handle_t)arg));
}

static void IRAM_ATTR UNUSED gpio_isr_endstop(void *arg) {
    // static char buf[9];
    ets_printf("PIN_BTN %s\n", gpio_get_level(PIN_BTN) ? "RISE" : "FALL");
#ifdef CONFIG_I2C_GPIOEXP
    LOOPN(i, LEN(i2c_pin_data)) {
        i2c_trans(NUM_I2C, i2c_pin_addr[i], I2C_MASTER_READ, i2c_pin_data + i, 1);
        // itoa(i2c_pin_data[idx], buf, 2);
        // ESP_LOGI(TAG, "I2C GPIO Expander value: 0b%s", buf);
    }
#endif // CONFIG_I2C_GPIOEXP
#ifdef CONFIG_SPI_GPIOEXP
    if (!spi_device_polling_transmit(spi_pin_hdlr, &spi_pin_trans)) {
        // ESP_LOGI(TAG, "SPI GPIO Expander value: 0b%s", buf);
    }
#endif // CONFIG_SPI_GPIOEXP
}

static void gpio_initialize() {
#if defined(CONFIG_I2C_GPIOEXP) || defined(CONFIG_SPI_GPIOEXP)
    gpio_config_t int_conf = {
        .pin_bit_mask = BIT64(PIN_INT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE
    };
    ESP_ERROR_CHECK( gpio_config(&int_conf) );
    ESP_ERROR_CHECK( gpio_install_isr_service(0) );
    ESP_ERROR_CHECK( gpio_isr_handler_add(PIN_INT, gpio_isr_endstop, NULL) );
#endif
#ifdef CONFIG_KNOB_INPUT
    knob_cb_t knob_funcs[] = { gpio_cb_left_rotate, gpio_cb_right_rotate };
    knob_event_t knob_evts[] = { KNOB_LEFT, KNOB_RIGHT };
    knob_config_t knob_conf = {
        .default_direction = 0,     // 0:positive; 1:negative
        .gpio_encoder_a = PIN_ENCA,
        .gpio_encoder_b = PIN_ENCB
    };
    if (!( knob = iot_knob_create(&knob_conf) )) {
        ESP_LOGE(TAG, "Knob: bind to GPIO%d & %d failed", PIN_ENCA, PIN_ENCB);
    } else LOOPN(i, LEN(knob_evts)) {
        iot_knob_register_cb(knob, knob_evts[i], knob_funcs[i], NULL);
    }
#endif
    button_cb_t btn_funcs[] = {
        gpio_cb_single_click, gpio_cb_double_click, gpio_cb_long_press
    };
    button_event_t btn_evts[] = {
        BUTTON_SINGLE_CLICK, BUTTON_DOUBLE_CLICK, BUTTON_LONG_PRESS_START
    };
    button_config_t btn_conf = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = PIN_BTN,
            .active_level = 1,
        }
    };
    if (!( btn = iot_button_create(&btn_conf) )) {
        ESP_LOGE(TAG, "BTN: bind to GPIO%d failed", PIN_BTN);
    } else LOOPN(i, LEN(btn_evts)) {
        iot_button_register_cb(btn, btn_evts[i], btn_funcs[i], NULL);
    }
}

esp_err_t gpioext_set_level(int pin, bool level) {
    if (GPIO_IS_VALID_GPIO(pin))    return gpio_set_level(pin, level);
    if (PIN_IS_I2CEXP(pin))         return i2c_gpio_set_level(pin, level);
    if (PIN_IS_SPIEXP(pin))         return spi_gpio_set_level(pin, level);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t gpioext_get_level(int pin, bool * level, bool sync) {
    if (GPIO_IS_VALID_GPIO(pin)) {
        *level = gpio_get_level(pin);
        return ESP_OK;
    }
    if (PIN_IS_I2CEXP(pin)) return i2c_gpio_get_level(pin, level, sync);
    if (PIN_IS_SPIEXP(pin)) return spi_gpio_get_level(pin, level, sync);
    return ESP_ERR_INVALID_ARG;
}

static const char * const gpio_default_usage[GPIO_PIN_COUNT] = {
    [0]         = "Strapping",
    [1]         = "UART TXD",
    [2]         = "Strapping",
    [3]         = "UART RXD",
    [34]        = "ADC1 Channel6",

    [PIN_LED]   = "LED",
    [PIN_BTN]   = "Button",
#if defined(CONFIG_I2C_GPIOEXP) || defined(CONFIG_SPI_GPIOEXP)
    [PIN_INT]   = "Interrupt",
#endif

    [PIN_SDA0]  = "I2C0 SDA",
    [PIN_SCL0]  = "I2C0 SCL",
    [PIN_SDA1]  = "I2C1 SDA",
    [PIN_SCL1]  = "I2C1 SCL",

    [PIN_HMISO] = "HSPI MISO",
    [PIN_HMOSI] = "HSPI MOSI",
    [PIN_HSCLK] = "HSPI SCLK",
    [PIN_HCS0]  = "HSPI CS0 (SDCard)",
#ifdef CONFIG_SPI_GPIOEXP
    [PIN_HCS1]  = "HSPI CS1 (GPIOExp)",
#endif

#ifdef CONFIG_PWM_SERVO
    [PIN_SVOH]  = "Servo Yaw",
    [PIN_SVOV]  = "Servo Pitch",
#endif
#ifdef CONFIG_KNOB_INPUT
    [PIN_ENCA]  = "Knob encoder A",
    [PIN_ENCB]  = "Knob encoder B",
#endif
};

void gpio_table(bool i2c, bool spi) {
    bool level;
    esp_err_t err;
    const char * value;
    for (gpio_num_t pin = 0; pin < GPIO_PIN_COUNT; pin++) {
        if (!pin)
            printf("Native GPIO %d-%d\nPIN Value Usage\n", 0, GPIO_PIN_COUNT - 1);
        if (!GPIO_IS_VALID_GPIO(pin))
            continue;
        value = gpio_get_level(pin) ? "HIGH" : "LOW";
        printf("%-3d %5s %s\n", pin, value, gpio_default_usage[pin] ?: "");
    }
    for (i2c_pin_num_t pin = PIN_I2C_MIN; i2c && pin < PIN_I2C_MAX; pin++) {
        if (pin == PIN_I2C_MIN)
            printf("\nI2C GPIO EXP %d-%d\nPIN Value\n", PIN_I2C_MIN, PIN_I2C_MAX - 1);
        err = i2c_gpio_get_level(pin, &level, false);
        value = err ? esp_err_to_name(err) : (level ? "HIGH" : "LOW");
        printf("%-3d %5s\n", pin, value);
    }
    for (spi_pin_num_t pin = PIN_SPI_MIN; spi && pin < PIN_SPI_MAX; pin++) {
        if (pin == PIN_SPI_MIN)
            printf("\nSPI GPIO EXP %d-%d\nPIN Value\n", PIN_SPI_MIN, PIN_SPI_MAX - 1);
        err = spi_gpio_get_level(pin, &level, false);
        value = err ? esp_err_to_name(err) : (level ? "HIGH" : "LOW");
        printf("%-3d %5s\n", pin, value);
    }
}

// Others

static void uart_initialize() {
    fflush(stdout); fflush(stderr);
    msleep(10);

    // UART driver configuration
    uart_config_t uart_conf = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .use_ref_tick = true
    };
    ESP_ERROR_CHECK( uart_param_config(NUM_UART, &uart_conf) );
    ESP_ERROR_CHECK( uart_driver_install(NUM_UART, 256, 0, 0, NULL, 0) );

    // Register UART to VFS and configure
    /* esp_vfs_dev_uart_register(); */
    esp_vfs_dev_uart_use_driver(NUM_UART);
    esp_vfs_dev_uart_port_set_rx_line_endings(NUM_UART, ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_uart_port_set_tx_line_endings(NUM_UART, ESP_LINE_ENDINGS_CRLF);
}

static void twdt_initialize() {
#ifdef CONFIG_TASK_WDT
    ESP_ERROR_CHECK(esp_task_wdt_init(5, false));

    // Idle tasks are created on each core automatically by RTOS scheduler
    // with the lowest possible priority (0). Our tasks have higher priority,
    // thus leaving almost no time for idle tasks to run. Disable WDT on them.
    #ifdef CONFIG_FREERTOS_UNICORE
    LOOPN(i, 1)
    #else
    LOOPN(i, 2)
    #endif // CONFIG_FREERTOS_UNICORE
    {
        TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(i);
        if (idle && !esp_task_wdt_status(idle) && !esp_task_wdt_delete(idle)) {
            ESP_LOGW(TAG, "Task IDLE%d @ CPU%d removed from WDT", i, i);
        }
    }
#endif // CONFIG_TASK_WDT
}

esp_err_t twdt_feed() {
#ifdef CONFIG_TASK_WDT
    return esp_task_wdt_reset();
#endif // CONFIG_TASK_WDT
    return ESP_OK;
}

void driver_initialize() {
    pwm_initialize();
    adc_initialize();
    led_initialize();

    i2c_initialize();
    vlx_initialize();
    als_initialize();
    scn_initialize();

    spi_initialize();
    gpio_initialize();
    uart_initialize();
    twdt_initialize();
}
