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
#define PIN_SDA     GPIO_NUMBER(CONFIG_GPIO_I2C_SDA)
#define PIN_SCL     GPIO_NUMBER(CONFIG_GPIO_I2C_SCL)

#define PIN_HMISO   GPIO_NUMBER(CONFIG_GPIO_HSPI_MISO)
#define PIN_HMOSI   GPIO_NUMBER(CONFIG_GPIO_HSPI_MOSI)
#define PIN_HSCLK   GPIO_NUMBER(CONFIG_GPIO_HSPI_SCLK)
#define PIN_HCS0    GPIO_NUMBER(CONFIG_GPIO_HSPI_CS0)
#define PIN_HCS1    GPIO_NUMBER(CONFIG_GPIO_HSPI_CS1)
#define PIN_HSDCD   GPIO_NUMBER(CONFIG_GPIO_HSPI_SDCD)

void driver_initialize();

esp_err_t led_blink(bool level);
esp_err_t led_toggle();

esp_err_t pwm_degree(int hdeg, int vdeg);
esp_err_t twdt_feed();
uint16_t vlx_probe();
void scn_progbar(uint8_t percent);

void i2c_detect();

/*
// We use PCF8574 for IO expansion: Endstops | Temprature | Valves

typedef enum {
    PIN_I2C_MIN = 99,

    // endstops
    PIN_XMIN, PIN_XMAX, PIN_YMIN, PIN_YMAX,
    PIN_ZMIN, PIN_ZMAX, PIN_EVAL, PIN_PROB,

    // temprature
    PIN_BED,  PIN_NOZ1, PIN_NOZ2, PIN_NOZ3,
    PIN_FAN1, PIN_FAN2, PIN_FAN3, PIN_RSV,

    // valves
    PIN_VLV1, PIN_VLV2, PIN_VLV3, PIN_VLV4,
    PIN_VLV5, PIN_VLV6, PIN_VLV7, PIN_VLV8,

    PIN_I2C_MAX
} i2c_pin_num_t;

// Transfer data with PCF8574. idx indicates index of { endstops, temp, valves }
esp_err_t i2c_set_val(uint8_t idx);
esp_err_t i2c_get_val(uint8_t idx);

esp_err_t i2c_gpio_set_level(i2c_pin_num_t pin, bool level);
uint8_t i2c_gpio_get_level(i2c_pin_num_t pin, bool sync = false);
*/

/*
// IO expansion by 74HC595 (SPI connection): Steppers
typedef enum {
    PIN_SPI_MIN = 199,

    // stepper direction & step (pulse)
    PIN_XDIR,  PIN_XSTEP,  PIN_YDIR,  PIN_YSTEP,  PIN_ZDIR,  PIN_ZSTEP,
    PIN_E1DIR, PIN_E1STEP, PIN_E2DIR, PIN_E2STEP, PIN_E3DIR, PIN_E3STEP,

    // enable | disable
    PIN_XYZEN, PIN_E1EN, PIN_E2EN,  PIN_E3EN,

    PIN_SPI_MAX
} spi_pin_num_t;

// Transfer data to 74HC595. Same like i2c_gpio_set_val
esp_err_t spi_gpio_flush();

esp_err_t spi_gpio_set_level(spi_pin_num_t pin, bool level);
uint8_t spi_gpio_get_level(spi_pin_num_t pin_num);
*/

#ifdef __cplusplus
}
#endif

#endif // _DRIVERS_H_
