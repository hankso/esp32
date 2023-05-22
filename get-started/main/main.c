/* Testing ESP-IDF by hank <hankso1106@gmail.com> */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "esp_log.h"
#include "esp_flash.h"
#include "esp_chip_info.h"

#include "led_strip.h"
#include "sdkconfig.h"

#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "vl53l0x.h"

#define I2C_PORT I2C_NUM_0
#define PIN_SDA 21
#define PIN_SCL 22

static const char *TAG = "hankso";

/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO  CONFIG_BLINK_GPIO
#define PWM_GPIO0   13
#define PWM_GPIO1   12

static u8g2_t scn;
static vl53l0x_t *vlx;

#ifdef CONFIG_BLINK_LED_RMT

static led_strip_handle_t led_strip;

static int led_init(void) {
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
    return 0;
}

static int led_blink(uint8_t state) {
    /* If the addressable LED is enabled */
    if (state) {
        /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
        led_strip_set_pixel(led_strip, 0, 16, 16, 16);
        /* Refresh the strip to send data */
        led_strip_refresh(led_strip);
    } else {
        /* Set all LED off to clear all pixels */
        led_strip_clear(led_strip);
    }
    return 0;
}

#elif CONFIG_BLINK_LED_GPIO

static int led_blink(uint8_t state) {
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, state);
    return 0;
}

static int led_init(void) {
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    return 0;
}

#endif

static int pwm_init(void) {
    ledc_timer_config_t timer_config = {
        .speed_mode         = LEDC_LOW_SPEED_MODE,
        .timer_num          = LEDC_TIMER_0,
        .duty_resolution    = LEDC_TIMER_10_BIT,
        .freq_hz            = 50, // 20ms
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    ledc_channel_config_t channel0_config = {
        .gpio_num           = PWM_GPIO0,
        .speed_mode         = LEDC_LOW_SPEED_MODE,
        .channel            = LEDC_CHANNEL_0,
        .timer_sel          = LEDC_TIMER_0,
        .duty               = 0
    };
    ledc_channel_config_t channel1_config = {
        .gpio_num           = PWM_GPIO1,
        .speed_mode         = LEDC_LOW_SPEED_MODE,
        .channel            = LEDC_CHANNEL_1,
        .timer_sel          = LEDC_TIMER_0,
        .duty               = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel0_config));
    ESP_ERROR_CHECK(ledc_channel_config(&channel1_config));
    return 0;
}

static int pwm_duty(int channel, int degree) {
    // mapping 0-180 deg to 0.5-2.5 ms
    static float offset = 0.5 / 20 * ((1 << 10) - 1);
    static float scale  = 2.0 / 20 * ((1 << 10) - 1) / 180;
    if (degree < 0)
        degree = 0;
    if (degree > 180)
        degree = 180;
    int duty = degree * scale + offset;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel));
    return duty;
}

static int vlx_init(vl53l0x_t ** vlxp) {
    vl53l0x_t *vlx = vl53l0x_config(I2C_PORT, PIN_SCL, PIN_SDA, -1, 0x29, 0);
    const char *err = vl53l0x_init(vlx);
    if (err) {
        ESP_LOGE(TAG, "Initialize VL53L0X failed: %s\n", err);
        vl53l0x_end(vlx);
        return 1;
    }
    *vlxp = vlx;
    return 0;
}

static uint16_t vlx_probe(vl53l0x_t *vlx) {
    TickType_t tick_start = xTaskGetTickCount();
    uint16_t result_mm = vl53l0x_readRangeSingleMillimeters(vlx);
    int took_ms = ((int)xTaskGetTickCount() - tick_start) * portTICK_PERIOD_MS;
    if (result_mm != (uint16_t)-1) {
        ESP_LOGI(TAG, "Range %u mm took %d ms\n", result_mm, took_ms);
    } else {
        ESP_LOGW(TAG, "Failed to measure range\n");
    }
    return result_mm;
}

static void log_chip_info(void) {
    uint32_t flash_size;
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Get flash size failed\n");
        return;
    }
    ESP_LOGI(TAG, "Chip %s with %d CPU core(s), WiFi%s%s, Rev %d.%d, %uMB %s flash",
            CONFIG_IDF_TARGET, chip_info.cores,
            chip_info.features & CHIP_FEATURE_BT ? "/BT" : "",
            chip_info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
            chip_info.revision / 100, chip_info.revision % 100,
            flash_size / 1024 / 1024,
            chip_info.features & CHIP_FEATURE_EMB_FLASH ? "embedded" : "external");
    ESP_LOGI(TAG, "Minimum free heap size: %u bytes\n",
            esp_get_minimum_free_heap_size());
}

static int scn_init(u8g2_t *scn) {
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.sda = PIN_SDA;
    u8g2_esp32_hal.scl = PIN_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        scn, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&scn->u8x8, 0x78);
    u8g2_InitDisplay(scn);
    u8g2_SetFont(scn, u8g2_font_ncenB08_tr);
    u8g2_SetPowerSave(scn, 0);
    return 0;
}

static void scn_progbar(u8g2_t *scn, uint8_t percent) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", percent);
    u8g2_ClearBuffer(scn);
    u8g2_DrawFrame(scn, 0, 20, 128, 6);
    u8g2_DrawBox(scn, 0, 20, 128 * percent / 100, 6);
    u8g2_DrawStr(scn, (128 - u8g2_GetStrWidth(scn, buf)) / 2, 28 + 8, buf);
    u8g2_SendBuffer(scn);
}

void app_main(void) {
    log_chip_info();

    /* Configure the peripheral according to the LED type */
    led_init();
    pwm_init();
    // vlx_init(&vlx);
    scn_init(&scn);
    scn_progbar(&scn, 0);

    uint8_t count = 0, state = 0;
    TickType_t
        tick_curr = 0,
        tick_next = xTaskGetTickCount(),
        tick_intval = CONFIG_BLINK_PERIOD * portTICK_PERIOD_MS;

    while (1) {
        /* Accurate time control */
        tick_curr = xTaskGetTickCount();
        if (tick_curr < tick_next)
            vTaskDelay(tick_next - tick_curr);
        tick_next += tick_intval;

        /* Toggle the LED state */
        led_blink(state = !state);
        ESP_LOGI(TAG, "Turning the LED %s!", state ? "ON" : "OFF");

        /* Measure distance */
        if (vlx) vlx_probe(vlx);

        count += 5;

        /* Draw progress bar on screen */
        scn_progbar(&scn, count / 255.0 * 100);

        /* Drive Servo by PWM */
        int deg_pitch = (count < 0x7F ? count : (0xFF - count)) / 128.0 * 95;
        int deg_yaw = count / 255.0 * 180;
        pwm_duty(LEDC_CHANNEL_0, deg_pitch);
        pwm_duty(LEDC_CHANNEL_1, deg_yaw);
    }
}
