/* 
 * File: drivers.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:53:43
 */

#ifndef _DRIVERS_H_
#define _DRIVERS_H_

#include "globals.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)

#define _UART_NUMBER(num) UART_NUM_##num
#define UART_NUMBER(num) _UART_NUMBER(num)

#define _GPIO_NUMBER(num) GPIO_NUM_##num
#define GPIO_NUMBER(num) _GPIO_NUMBER(num)

#define NUM_I2C     I2C_NUMBER(CONFIG_I2C_NUM)
#define NUM_UART    UART_NUMBER(CONFIG_UART_NUM)

#define PIN_LED     GPIO_NUMBER(CONFIG_GPIO_LED)
#define PIN_SVOH    GPIO_NUMBER(CONFIG_GPIO_SERVOH)
#define PIN_SVOV    GPIO_NUMBER(CONFIG_GPIO_SERVOV)
#define PIN_SDA0    GPIO_NUMBER(CONFIG_GPIO_I2C_SDA)
#define PIN_SCL0    GPIO_NUMBER(CONFIG_GPIO_I2C_SCL)
#define PIN_SDA1    GPIO_NUMBER(CONFIG_GPIO_SCN_SDA)
#define PIN_SCL1    GPIO_NUMBER(CONFIG_GPIO_SCN_SCL)

#define PIN_HMISO   GPIO_NUMBER(CONFIG_GPIO_HSPI_MISO)
#define PIN_HMOSI   GPIO_NUMBER(CONFIG_GPIO_HSPI_MOSI)
#define PIN_HSCLK   GPIO_NUMBER(CONFIG_GPIO_HSPI_SCLK)
#define PIN_HCS0    GPIO_NUMBER(CONFIG_GPIO_HSPI_CS0)
#define PIN_HCS1    GPIO_NUMBER(CONFIG_GPIO_HSPI_CS1)

void driver_initialize();

esp_err_t led_set_light(int index, float brightness);
float led_get_light(int index);
esp_err_t led_set_color(int index, uint32_t color);
uint32_t led_get_color(int index);

void gpio_table(bool i2c, bool spi);
esp_err_t gpioext_set_level(int pin, bool level);
esp_err_t gpioext_get_level(int pin, bool * level, bool sync);

esp_err_t pwm_degree(int hdeg, int vdeg);

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
    float humidity;     // percentage
    float altitude;     // meter
} gy39_data_t;

esp_err_t gy39_measure(int bus, gy39_data_t *dat);

void scn_progbar(uint8_t percent);

void i2c_detect(int bus);
esp_err_t smbus_probe(int bus, uint8_t addr);
esp_err_t smbus_dump(int bus, uint8_t addr, uint8_t reg_start, uint8_t reg_end);
esp_err_t smbus_write_byte(int bus, uint8_t addr, uint8_t reg, uint8_t val);
esp_err_t smbus_read_byte(int bus, uint8_t addr, uint8_t reg, uint8_t *val);
esp_err_t smbus_write_word(int bus, uint8_t addr, uint8_t reg, uint16_t val);
esp_err_t smbus_read_word(int bus, uint8_t addr, uint8_t reg, uint16_t *val);

// We use PCF8574 for IO expansion: Endstops | Temprature | Valves

typedef enum {
    PIN_I2C_MIN = 99,

/*
    // endstops
    PIN_XMIN, PIN_XMAX, PIN_YMIN, PIN_YMAX,
    PIN_ZMIN, PIN_ZMAX, PIN_EVAL, PIN_PROB,

    // temprature
    PIN_BED,  PIN_NOZ1, PIN_NOZ2, PIN_NOZ3,
    PIN_FAN1, PIN_FAN2, PIN_FAN3, PIN_RSV,

    // valves
    PIN_VLV1, PIN_VLV2, PIN_VLV3, PIN_VLV4,
    PIN_VLV5, PIN_VLV6, PIN_VLV7, PIN_VLV8,
*/

    PIN_I2C_MAX
} i2c_pin_num_t;

esp_err_t i2c_gpio_set_level(i2c_pin_num_t pin, bool level);
esp_err_t i2c_gpio_get_level(i2c_pin_num_t pin, bool * level, bool sync);

// IO expansion by 74HC595 (SPI connection): Steppers
typedef enum {
    PIN_SPI_MIN = 199,

/*
    // stepper direction & step (pulse)
    PIN_XDIR,  PIN_XSTEP,  PIN_YDIR,  PIN_YSTEP,  PIN_ZDIR,  PIN_ZSTEP,
    PIN_E1DIR, PIN_E1STEP, PIN_E2DIR, PIN_E2STEP, PIN_E3DIR, PIN_E3STEP,

    // enable | disable
    PIN_XYZEN, PIN_E1EN, PIN_E2EN,  PIN_E3EN,
*/

    PIN_SPI_MAX
} spi_pin_num_t;

esp_err_t spi_gpio_set_level(spi_pin_num_t pin, bool level);
esp_err_t spi_gpio_get_level(spi_pin_num_t pin, bool * level, bool sync);

#ifdef __cplusplus
}
#endif

#endif // _DRIVERS_H_
