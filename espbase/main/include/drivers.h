/* 
 * File: drivers.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:53:43
 */

#pragma once

#include "globals.h"

#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#if defined(CONFIG_USE_BTN) && !__has_include("iot_button.h")
#   warning "Run `idf.py add-dependency espressif/button`"
#   undef CONFIG_USE_BTN
#endif

#if defined(CONFIG_USE_KNOB) && !__has_include("iot_knob.h")
#   warning "Run `idf.py add-dependency espressif/knob`"
#   undef CONFIG_USE_KNOB
#endif

#if defined(CONFIG_LED_INDICATOR) && !__has_include("led_indicator.h")
#   warning "Run `idf.py add-dependency espressif/led_indicator`"
#   undef CONFIG_LED_INDICATOR
#endif

#if defined(CONFIG_VLX_SENSOR) && !__has_include("vl53l0x.h")
#   warning "Run `git clone git@github.com:revk/ESP32-VL53L0X`"
#   undef CONFIG_VLX_SENSOR
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)

#define _SPI_NUMBER(num) SPI##num##_HOST
#define SPI_NUMBER(num) _SPI_NUMBER(num)

#define _UART_NUMBER(num) UART_NUM_##num
#define UART_NUMBER(num) _UART_NUMBER(num)

#define _GPIO_NUMBER(num) GPIO_NUM_##num
#define GPIO_NUMBER(num) _GPIO_NUMBER(num)

#ifdef CONFIG_USE_LED
#   define PIN_LED      GPIO_NUMBER(CONFIG_GPIO_LED)
#endif

#ifdef CONFIG_USE_UART
#   define NUM_UART     UART_NUMBER(CONFIG_UART_NUM)
#   define PIN_TXD      GPIO_NUMBER(CONFIG_GPIO_TXD)
#   define PIN_RXD      GPIO_NUMBER(CONFIG_GPIO_RXD)
#   define PIN_RTS      UART_PIN_NO_CHANGE
#   define PIN_CTS      UART_PIN_NO_CHANGE
#endif

#ifdef CONFIG_USE_I2C
#   define NUM_I2C      I2C_NUMBER(CONFIG_I2C_NUM)
#   define PIN_SDA      GPIO_NUMBER(CONFIG_GPIO_SDA)
#   define PIN_SCL      GPIO_NUMBER(CONFIG_GPIO_SCL)
#endif
#ifdef CONFIG_USE_I2C1
#   define PIN_SDA1     GPIO_NUMBER(CONFIG_GPIO_SDA1)
#   define PIN_SCL1     GPIO_NUMBER(CONFIG_GPIO_SCL1)
#else
#   define PIN_SDA1     PIN_SDA
#   define PIN_SCL1     PIN_SCL
#endif

#ifdef CONFIG_USE_SPI
#   define NUM_SPI      SPI_NUMBER(CONFIG_SPI_NUM)
#   define PIN_MISO     GPIO_NUMBER(CONFIG_GPIO_SPI_MISO)
#   define PIN_MOSI     GPIO_NUMBER(CONFIG_GPIO_SPI_MOSI)
#   define PIN_SCLK     GPIO_NUMBER(CONFIG_GPIO_SPI_SCLK)
#endif
#ifdef CONFIG_USE_SDFS
#   define PIN_CS0      GPIO_NUMBER(CONFIG_GPIO_SPI_CS0)
#endif
#ifdef CONFIG_USE_SPI_GPIOEXP
#   define PIN_CS1      GPIO_NUMBER(CONFIG_GPIO_SPI_CS1)
#endif

#ifdef CONFIG_USE_GPIOEXP
#   define PIN_INT      GPIO_NUMBER(CONFIG_GPIO_INT)
#endif

#ifdef CONFIG_USE_ADC
#   define PIN_ADC      GPIO_NUMBER(CONFIG_GPIO_ADC)
#endif

#ifdef CONFIG_USE_BTN
#   define PIN_BTN      GPIO_NUMBER(CONFIG_GPIO_BTN)
#endif

#ifdef CONFIG_USE_KNOB
#   define PIN_ENCA     GPIO_NUMBER(CONFIG_GPIO_ENCA)
#   define PIN_ENCB     GPIO_NUMBER(CONFIG_GPIO_ENCB)
#endif

#ifdef CONFIG_USE_SERVO
#   define PIN_SVOH     GPIO_NUMBER(CONFIG_GPIO_SERVOH)
#   define PIN_SVOV     GPIO_NUMBER(CONFIG_GPIO_SERVOV)
#endif

#ifdef CONFIG_USE_BUZZER
#   define PIN_BUZZ     GPIO_NUMBER(CONFIG_GPIO_BUZZER)
#endif

void driver_initialize();

esp_err_t led_set_light(int index, uint8_t brightness);
uint8_t led_get_light(int index);
esp_err_t led_set_color(int index, uint32_t color);
uint32_t led_get_color(int index);
esp_err_t led_set_blink(int blink_type);

uint32_t adc_read();

void gpio_table(bool i2c, bool spi);
esp_err_t gpioext_set_level(int pin, bool level);
esp_err_t gpioext_get_level(int pin, bool * level, bool sync);

esp_err_t pwm_set_degree(int hdeg, int vdeg);
esp_err_t pwm_get_degree(int *hdeg, int *vdeg);
esp_err_t pwm_set_tone(int freq, int pcent);
esp_err_t pwm_get_tone(int *freq, int *pcent);

esp_err_t twdt_feed();

uint16_t vlx_probe();

typedef enum {
    ALS_TRACK_0,    // single input
    ALS_TRACK_1,
    ALS_TRACK_2,
    ALS_TRACK_3,
    ALS_TRACK_H,    // dual input
    ALS_TRACK_V,
    ALS_TRACK_A,    // quad input
} als_track_t;

esp_err_t als_tracking(als_track_t method, int *hdeg, int *vdeg);

float als_brightness(int idx);

typedef struct {
    float brightness;   // lux
    float temperature;  // Celsius degree
    float atmosphere;   // Pa
    float humidity;     // 0-1 percentage
    float altitude;     // meter
} gy39_data_t;

esp_err_t gy39_measure(gy39_data_t *dat);

esp_err_t scn_progbar(uint8_t percent);

void i2c_detect(int bus);

esp_err_t smbus_probe(int bus, uint8_t addr);
esp_err_t smbus_dump(int bus, uint8_t addr, uint8_t reg_start, uint8_t reg_end);
esp_err_t smbus_write_byte(int bus, uint8_t addr, uint8_t reg, uint8_t val);
esp_err_t smbus_read_byte(int bus, uint8_t addr, uint8_t reg, uint8_t *val);
esp_err_t smbus_write_word(int bus, uint8_t addr, uint8_t reg, uint16_t val);
esp_err_t smbus_read_word(int bus, uint8_t addr, uint8_t reg, uint16_t *val);

// We use PCF8574 for IO expansion: Endstops | Temprature | Valves

typedef enum {
    PIN_I2C_BASE = GPIO_PIN_COUNT - 1,

#ifdef CONFIG_USE_I2C_GPIOEXP
    // endstops
    PIN_XMIN, PIN_XMAX, PIN_YMIN, PIN_YMAX,
    PIN_ZMIN, PIN_ZMAX, PIN_EVAL, PIN_PROB,

    // temprature
    PIN_BED,  PIN_NOZ1, PIN_NOZ2, PIN_NOZ3,
    PIN_FAN1, PIN_FAN2, PIN_FAN3, PIN_RSV,

    // valves
    PIN_VLV1, PIN_VLV2, PIN_VLV3, PIN_VLV4,
    PIN_VLV5, PIN_VLV6, PIN_VLV7, PIN_VLV8,
#endif

    PIN_I2C_MAX, PIN_I2C_MIN = PIN_I2C_BASE + 1
} i2c_pin_num_t;

#define PIN_IS_I2CEXP(n) ( (n) >= PIN_I2C_MIN && (n) < PIN_I2C_MAX )

esp_err_t i2c_gpio_set_level(i2c_pin_num_t pin, bool level);
esp_err_t i2c_gpio_get_level(i2c_pin_num_t pin, bool * level, bool sync);

// IO expansion by 74HC595 (SPI connection): Steppers

typedef enum {
    PIN_SPI_BASE = PIN_I2C_MAX - 1,

#ifdef CONFIG_USE_SPI_GPIOEXP
    // stepper direction & step (pulse)
    PIN_XDIR,  PIN_XSTEP,  PIN_YDIR,  PIN_YSTEP,  PIN_ZDIR,  PIN_ZSTEP,
    PIN_E1DIR, PIN_E1STEP, PIN_E2DIR, PIN_E2STEP, PIN_E3DIR, PIN_E3STEP,

    // enable | disable
    PIN_XYZEN, PIN_E1EN, PIN_E2EN,  PIN_E3EN,
#endif

    PIN_SPI_MAX, PIN_SPI_MIN = PIN_SPI_BASE + 1
} spi_pin_num_t;

#define PIN_IS_SPIEXP(n) ( (n) >= PIN_SPI_MIN && (n) < PIN_SPI_MAX )

esp_err_t spi_gpio_set_level(spi_pin_num_t pin, bool level);
esp_err_t spi_gpio_get_level(spi_pin_num_t pin, bool * level, bool sync);

#ifdef __cplusplus
}
#endif
