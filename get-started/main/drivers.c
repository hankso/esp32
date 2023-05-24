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
#include "esp_intr_alloc.h"
#include "esp_task_wdt.h"
#include "soc/soc.h"
#include "sys/param.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#if __has_include("vl53l0x.h")
#    include "vl53l0x.h"
#    define WITH_VLX
#endif
#include "led_strip.h"

static const char *TAG = "Drivers";

#ifdef CONFIG_BLINK_LED_RMT

static led_strip_t * led_strip;

static void led_initialize() {
    led_strip = led_strip_init(CONFIG_BLINK_LED_RMT_CHANNEL, PIN_LED, 1);
    if (led_strip)
        led_strip->clear(led_strip, 50); // timeout = 50ms
}

esp_err_t led_blink(uint32_t level) {
    if (!led_strip)
        return ESP_ERR_INVALID_STATE;
    if (state) {
        led_strip->set_pixel(led_strip, 0, 0x0F, 0x0F, 0x0F);
        return led_strip->refresh(led_strip, 100);
    }
    return led_strip->clear(led_strip, 50);
}

esp_err_t led_toggle() { return ESP_ERR_INVALID_ARG; }

#elif CONFIG_BLINK_LED_GPIO

static void led_initialize() {
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_INPUT_OUTPUT);
}

esp_err_t led_blink(bool level) {
    return gpio_set_level(PIN_LED, level);
}

esp_err_t led_toggle() {
    return gpio_set_level(PIN_LED, !gpio_get_level(PIN_LED));
}

#endif // CONFIG_BLINK_LED

// PWM for Servo

// mapping 0-180 deg to 0.5-2.5 ms by 10bit resolution
static float duty_offset = 0.5 / 20 * ((1 << 10) - 1);
static float duty_scale  = 2.0 / 20 * ((1 << 10) - 1) / 180;

static void pwm_initialize() {
    ledc_timer_config_t timer_config = {
        .speed_mode         = LEDC_LOW_SPEED_MODE,
        .timer_num          = LEDC_TIMER_0,
        .duty_resolution    = LEDC_TIMER_10_BIT,
        .freq_hz            = 50, // 20ms
        .clk_cfg            = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));
    ledc_channel_config_t channel0_config = {
        .gpio_num           = PIN_SVOH,
        .speed_mode         = LEDC_LOW_SPEED_MODE,
        .channel            = LEDC_CHANNEL_0,
        .timer_sel          = LEDC_TIMER_0,
        .duty               = 0
    };
    ledc_channel_config_t channel1_config = {
        .gpio_num           = PIN_SVOV,
        .speed_mode         = LEDC_LOW_SPEED_MODE,
        .channel            = LEDC_CHANNEL_1,
        .timer_sel          = LEDC_TIMER_0,
        .duty               = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel0_config));
    ESP_ERROR_CHECK(ledc_channel_config(&channel1_config));
}

static esp_err_t pwm_duty(ledc_channel_t channel, int degree) {
    int duty = degree * duty_scale + duty_offset;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (!err) err = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    return err;
}

esp_err_t pwm_degree(int hdeg, int vdeg) {
    esp_err_t err = pwm_duty(LEDC_CHANNEL_0, hdeg);
    if (!err) err = pwm_duty(LEDC_CHANNEL_1, vdeg);
    return err;
}

// I2C GPIO Expander

static void i2c_initialize() {
    i2c_config_t master_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = PIN_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
    };
    master_conf.master.clk_speed = 50 * 1000; // 50KHz
    ESP_ERROR_CHECK( i2c_param_config(NUM_I2C, &master_conf) );
    ESP_ERROR_CHECK( i2c_driver_install(NUM_I2C, master_conf.mode, 0, 0, 0) );
}

static esp_err_t i2c_master_transfer(uint8_t addr, uint8_t rw, uint8_t *data, size_t size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | rw, true);
    if (rw == I2C_MASTER_WRITE) {
        if (size > 1) {
            i2c_master_write(cmd, data, size, true);
        } else if (size && data != NULL) {
            i2c_master_write_byte(cmd, *data, true);
        }
    } else if (rw == I2C_MASTER_READ) {
        if (size > 1) {
            i2c_master_read(cmd, data, size, I2C_MASTER_LAST_NACK);
        } else if (size && data != NULL) {
            i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
        }
    }
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(NUM_I2C, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

void i2c_detect() {
    for (uint8_t i = 0; i < 0x10; i++) {
        if (!i) printf("  ");
        printf("  %c", i < 10 ? (i + '0') : (i - 10 + 'A'));
    }
    for (uint8_t addr = 0; addr < 0x80; addr++) {
        if (addr % 0x10 == 0) printf("\n%02X", addr);
        esp_err_t ret = i2c_master_transfer(addr, I2C_MASTER_WRITE, NULL, 0);
        switch (ret) {
        case ESP_OK:
            printf(" %02X", addr); break;
        case ESP_ERR_TIMEOUT:
            printf(" UU"); break;
        default:
            printf(" --");
        }
    }
}

// static uint8_t i2c_pin_data[3] = { 0, 0, 0 };
// static uint8_t i2c_pin_addr[3] = { 0b0100000, 0b0100001, 0b0100010 };
//
// esp_err_t i2c_gpio_set_level(i2c_pin_num_t pin_num, bool level) {
//     uint8_t
//         pin = pin_num - PIN_I2C_MIN - 1,
//         idx = pin >> 3, bit = pin & 0x7,
//         addr = i2c_pin_addr[idx],
//         *data = i2c_pin_data + idx;
//     bitWrite(*data, bit, level);
//     return i2c_master_transfer(addr, I2C_MASTER_WRITE, data, 1);
// }
//
// uint8_t i2c_gpio_get_level(i2c_pin_num_t pin_num, bool sync) {
//     uint8_t
//         pin = pin_num - PIN_I2C_MIN - 1,
//         idx = pin >> 3, bit = pin & 0x7,
//         *data = i2c_pin_data + idx;
//     if (sync) i2c_master_transfer(i2c_pin_addr[idx], I2C_MASTER_READ, data, 1);
//     return bitRead(*data, bit);
// }

// I2C Distance Measurement

#ifdef WITH_VLX

static vl53l0x_t *vlx;

static void vlx_initialize() {
    vlx = vl53l0x_config(NUM_I2C, PIN_SCL, PIN_SDA, -1, 0x29, 0);
    const char *err = vl53l0x_init(vlx);
    if (err) {
        ESP_LOGE(TAG, "Initialize VL53L0X failed: %s\n", err);
        vl53l0x_end(vlx);
        vlx = NULL;
    }
}

uint16_t vlx_probe() {
    TickType_t tick_start = xTaskGetTickCount();
    uint16_t result_mm = vl53l0x_readRangeSingleMillimeters(vlx);
    int took_ms = ((int)xTaskGetTickCount() - tick_start) * portTICK_PERIOD_MS;
    if (result_mm != (uint16_t)-1) {
        ESP_LOGD(TAG, "Range %u mm took %d ms\n", result_mm, took_ms);
    } else {
        ESP_LOGW(TAG, "Failed to measure range\n");
    }
    return result_mm;
}

#else // WITH_VLX

void vlx_initialize() { ESP_LOGE(TAG, "VLX sensor not supported"); }
uint16_t vlx_probe() { return 0; }

#endif // WITH_VLX

// I2C OLED Screen

static u8g2_t scn;

static void scn_initialize() {
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.sda = PIN_SDA;
    u8g2_esp32_hal.scl = PIN_SCL;
    u8g2_esp32_hal_init(u8g2_esp32_hal);
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &scn, U8G2_R0,
        u8g2_esp32_i2c_byte_cb,
        u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&scn.u8x8, 0x78);
    u8g2_InitDisplay(&scn);
    u8g2_SetFont(&scn, u8g2_font_ncenB08_tr);
    u8g2_SetPowerSave(&scn, 0);
}

void scn_progbar(uint8_t percent) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", percent);
    u8g2_ClearBuffer(&scn);
    u8g2_DrawFrame(&scn, 0, 20, 128, 6);
    u8g2_DrawBox(&scn, 0, 20, 128 * percent / 100, 6);
    u8g2_DrawStr(&scn, (128 - u8g2_GetStrWidth(&scn, buf)) / 2, 28 + 8, buf);
    u8g2_SendBuffer(&scn);
}

// GPIO Interrupt

// static void IRAM_ATTR gpio_isr_endstop(void *arg) {
//     i2c_master_transfer(i2c_pin_addr[0], I2C_MASTER_READ, i2c_pin_data + 0, 1);
//     static char buf[9]; itoa(i2c_pin_data[0], buf, 2);
//     printf("Endstops value: 0b%s", buf);
// }
//
// static void gpio_initialize() {
//     gpio_config_t inp_conf = {
//         .pin_bit_mask = BIT64(PIN_INT),
//         .mode         = GPIO_MODE_INPUT,
//         .pull_up_en   = GPIO_PULLUP_ENABLE,
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .intr_type    = GPIO_INTR_NEGEDGE,
//     };
//     ESP_ERROR_CHECK( gpio_config(&inp_conf) );
//     ESP_ERROR_CHECK( gpio_install_isr_service(0) );
//     ESP_ERROR_CHECK( gpio_isr_handler_add(PIN_INT, gpio_isr_endstop, NULL) );
// }

// If transmitted data is 32bits or less, spi_transaction_t can use tx_data.
// Here we have no more than 4 chips, thus SPI_TRANS_USE_TXDATA.
// static spi_device_handle_t spi_pin_hdlr;
// static spi_transaction_t spi_pin_trans;
// static uint8_t spi_pin_data[2] = { 0, 0 };
//
// static void spi_initialize() {
//     spi_bus_config_t hspi_busconf = {
//         .mosi_io_num = PIN_HMOSI,
//         .miso_io_num = PIN_HMISO,
//         .sclk_io_num = PIN_HSCLK,
//         .quadwp_io_num = -1,
//         .quadhd_io_num = -1,
//         .max_transfer_sz = 0,
//         .flags = SPICOMMON_BUSFLAG_MASTER,
//         .intr_flags = 0
//     };
//     spi_device_interface_config_t devconf = {
//         .command_bits = 0,
//         .address_bits = 0,
//         .dummy_bits = 0,
//         .mode = 0b10, // CPOL = 1, CPHA = 0
//         .duty_cycle_pos = 128, // 128/255 = 50% (Tlow equals to Thigh)
//         .cs_ena_pretrans = 0,
//         .cs_ena_posttrans = 0,
//         .clock_speed_hz = 5 * 1000 * 1000, // 5MHz
//         .input_delay_ns = 0,
//         .spics_io_num = PIN_HCS1,
//         .flags = 0,
//         .queue_size = 1, // only one transaction exists in the queue
//         .pre_cb = NULL,
//         .post_cb = NULL
//     };
//     esp_err_t err = spi_bus_initialize(HSPI_HOST, &hspi_busconf, 1);
//     assert((!err || err == ESP_ERR_INVALID_STATE) && "SPI init failed");
//     ESP_ERROR_CHECK( spi_bus_add_device(HSPI_HOST, &devconf, &spi_pin_hdlr) );
//     uint8_t spi_pin_data_len = sizeof(spi_pin_data) / sizeof(spi_pin_data[0]);
//     spi_pin_trans.length = spi_pin_data_len * 8; // in bits;
//     // if (spi_pin_data_len <= 4) {
//     //     spi_pin_data = spi_pin_trans.tx_data;
//     //     for (uint16_t i = 0; i < spi_pin_data_len; i++) spi_pin_data[i] = 0;
//     //     spi_pin_trans.flags = SPI_TRANS_USE_TXDATA;
//     // } else {
//         spi_pin_trans.tx_buffer = spi_pin_data;
//     // }
// }
//
// esp_err_t spi_gpio_flush() {
//     return spi_device_polling_transmit(spi_pin_hdlr, &spi_pin_trans);
// }
//
// esp_err_t spi_gpio_set_level(spi_pin_num_t pin_num, bool level) {
//     uint8_t pin = pin_num - PIN_SPI_MIN - 1, idx = pin >> 3, bit = pin & 0x7;
//     bitWrite(spi_pin_data[idx], bit, level);
//     return spi_gpio_flush();
// }
//
// uint8_t spi_gpio_get_level(spi_pin_num_t pin_num) {
//     uint8_t pin = pin_num - PIN_SPI_MIN - 1, idx = pin >> 3, bit = pin & 0x7;
//     return bitRead(spi_pin_data[idx], bit);
// }

// Others

static void uart_initialize() {
    fflush(stdout); fflush(stderr);
    vTaskDelay(pdMS_TO_TICKS(10));
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
    #ifndef CONFIG_FREERTOS_UNICORE
    uint8_t num = 2;
    #else
    uint8_t num = 1;
    #endif // CONFIG_FREERTOS_UNICORE
    while (num--) {
        TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(num);
        if (idle && !esp_task_wdt_status(idle) && !esp_task_wdt_delete(idle)) {
            ESP_LOGW(TAG, "Task IDLE%d @ CPU%d removed from WDT", num, num);
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
    led_initialize();
    pwm_initialize();
    i2c_initialize();
    // spi_initialize();
    uart_initialize();
    // gpio_initialize();
}
