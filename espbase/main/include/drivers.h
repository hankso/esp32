/* 
 * File: drivers.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:53:43
 */

#pragma once

#include "globals.h"

#include "driver/i2c.h"         // for I2C_NUM_XXX
#include "driver/uart.h"        // for UART_NUM_XXX
#include "driver/gpio.h"        // for GPIO_NUM_XXX
#include "driver/spi_master.h"  // for SPIXXX_HOST

#if defined(CONFIG_BASE_USE_BTN) && !__has_include("iot_button.h")
#   warning "Run `idf.py add-dependency espressif/button`"
#   undef CONFIG_BASE_USE_BTN
#endif

#if defined(CONFIG_BASE_USE_KNOB) && !__has_include("iot_knob.h")
#   warning "Run `idf.py add-dependency espressif/knob`"
#   undef CONFIG_BASE_USE_KNOB
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

#define PIN_UNUSED      -1

#ifdef CONFIG_BASE_USE_LED
#   define PIN_LED      GPIO_NUMBER(CONFIG_BASE_GPIO_LED)
#endif

#ifdef CONFIG_BASE_USE_UART
#   define NUM_UART     UART_NUMBER(CONFIG_BASE_UART_NUM)
#   define PIN_TXD      GPIO_NUMBER(CONFIG_BASE_GPIO_TXD)
#   define PIN_RXD      GPIO_NUMBER(CONFIG_BASE_GPIO_RXD)
#   define PIN_RTS      UART_PIN_NO_CHANGE
#   define PIN_CTS      UART_PIN_NO_CHANGE
#endif

#ifdef CONFIG_BASE_USE_I2C
#   define NUM_I2C      I2C_NUMBER(CONFIG_BASE_I2C_NUM)
#   if CONFIG_BASE_I2C_NUM == 0
#       define PIN_SDA  PIN_SDA0
#       define PIN_SCL  PIN_SCL0
#   else
#       define PIN_SDA  PIN_SDA1
#       define PIN_SCL  PIN_SCL1
#   endif
#endif
#ifdef CONFIG_BASE_USE_I2C0
#   define PIN_SDA0     GPIO_NUMBER(CONFIG_BASE_GPIO_SDA0)
#   define PIN_SCL0     GPIO_NUMBER(CONFIG_BASE_GPIO_SCL0)
#endif
#ifdef CONFIG_BASE_USE_I2C1
#   define PIN_SDA1     GPIO_NUMBER(CONFIG_BASE_GPIO_SDA1)
#   define PIN_SCL1     GPIO_NUMBER(CONFIG_BASE_GPIO_SCL1)
#endif

#ifdef CONFIG_BASE_USE_SPI
#   define NUM_SPI      SPI_NUMBER(CONFIG_BASE_SPI_NUM)
#   define PIN_MISO     GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_MISO)
#   define PIN_MOSI     GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_MOSI)
#   define PIN_SCLK     GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_SCLK)
#endif
#ifdef CONFIG_BASE_USE_SDFS
#   define PIN_CS0      GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_CS0)
#endif
#ifdef CONFIG_BASE_SCREEN_SPI
#   define PIN_CS1      GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_CS1)
#   define PIN_SDC      GPIO_NUMBER(CONFIG_BASE_GPIO_SCN_DC)
#   ifdef CONFIG_BASE_GPIO_SCN_RST
#       define PIN_SRST GPIO_NUMBER(CONFIG_BASE_GPIO_SCN_RST)
#   else
#       define PIN_SRST PIN_UNUSED
#   endif
#endif
#ifdef CONFIG_BASE_GPIOEXP_SPI
#   define PIN_CS2      GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_CS2)
#endif

#ifdef CONFIG_BASE_USE_GPIOEXP
#   define PIN_INT      GPIO_NUMBER(CONFIG_BASE_GPIO_INT)
#endif

#if defined(CONFIG_BASE_ADC_HALL)
#   define PIN_ADC1     GPIO_NUMBER(36)
#   define PIN_ADC2     GPIO_NUMBER(39)
#elif defined(CONFIG_BASE_ADC_JOYSTICK)
#   define PIN_ADC1     GPIO_NUMBER(CONFIG_BASE_GPIO_ADC1)
#   define PIN_ADC2     GPIO_NUMBER(CONFIG_BASE_GPIO_ADC2)
#elif defined(CONFIG_BASE_USE_ADC)
#   define PIN_ADC1     GPIO_NUMBER(CONFIG_BASE_GPIO_ADC1)
#endif

#ifdef CONFIG_BASE_USE_DAC
#   define PIN_DAC      GPIO_NUMBER(CONFIG_BASE_GPIO_DAC)
#endif

#ifdef CONFIG_BASE_USE_TPAD
#   define PIN_TPAD     GPIO_NUMBER(CONFIG_BASE_GPIO_TPAD)
#endif

#ifdef CONFIG_BASE_USE_BTN
#   define PIN_BTN      GPIO_NUMBER(CONFIG_BASE_GPIO_BTN)
#endif

#ifdef CONFIG_BASE_USE_KNOB
#   define PIN_ENCA     GPIO_NUMBER(CONFIG_BASE_GPIO_ENCA)
#   define PIN_ENCB     GPIO_NUMBER(CONFIG_BASE_GPIO_ENCB)
#endif

#ifdef CONFIG_BASE_USE_SERVO
#   define PIN_SVOH     GPIO_NUMBER(CONFIG_BASE_GPIO_SERVOH)
#   define PIN_SVOV     GPIO_NUMBER(CONFIG_BASE_GPIO_SERVOV)
#endif

#ifdef CONFIG_BASE_USE_BUZZER
#   define PIN_BUZZ     GPIO_NUMBER(CONFIG_BASE_GPIO_BUZZER)
#endif

void driver_initialize();

esp_err_t twdt_feed();

int adc_hall(); // return 0 if error
int adc_read(uint8_t idx); // return -1 if error, else measured mV
int adc_joystick(int *dx, int *dy); // return -1 if error, else x << 16 | y

esp_err_t dac_write(uint8_t val);
esp_err_t dac_cwave(uint32_t freq_scale_offset);

esp_err_t pwm_set_degree(int hdeg, int vdeg);
esp_err_t pwm_get_degree(int *hdeg, int *vdeg);
esp_err_t pwm_set_tone(int freq, int pcent);
esp_err_t pwm_get_tone(int *freq, int *pcent);

void i2c_detect(int bus);
esp_err_t smbus_probe(int bus, uint8_t addr);
esp_err_t smbus_wregs(int bus, uint8_t addr, uint8_t reg, uint8_t *, size_t);
esp_err_t smbus_rregs(int bus, uint8_t addr, uint8_t reg, uint8_t *, size_t);
esp_err_t smbus_dump(int bus, uint8_t addr, uint8_t reg, uint8_t num);
esp_err_t smbus_write_byte(int bus, uint8_t addr, uint8_t reg, uint8_t val);
esp_err_t smbus_read_byte(int bus, uint8_t addr, uint8_t reg, uint8_t *val);
esp_err_t smbus_write_word(int bus, uint8_t addr, uint8_t reg, uint16_t val);
esp_err_t smbus_read_word(int bus, uint8_t addr, uint8_t reg, uint16_t *val);

typedef enum {
    // GPIO Expander by PCF8574: Endstops | Temprature | Valves
    PIN_I2C_MIN = GPIO_PIN_COUNT - 1,
#ifdef CONFIG_BASE_GPIOEXP_I2C
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
    PIN_I2C_MAX,

    // GPIO Expander by cascaded 74HC595s (SPI connection): Steppers
    PIN_SPI_MIN = PIN_I2C_MAX - 1,
#ifdef CONFIG_BASE_GPIOEXP_SPI
    // stepper direction & step (pulse)
    PIN_XDIR,  PIN_XSTEP,  PIN_YDIR,  PIN_YSTEP,  PIN_ZDIR,  PIN_ZSTEP,
    PIN_E1DIR, PIN_E1STEP, PIN_E2DIR, PIN_E2STEP, PIN_E3DIR, PIN_E3STEP,

    // enable | disable
    PIN_XYZEN, PIN_E1EN, PIN_E2EN,  PIN_E3EN,
#endif
    PIN_SPI_MAX,
} gexp_num_t;

#define PIN_I2C_BASE        ( PIN_I2C_MIN + 1 )
#define PIN_SPI_BASE        ( PIN_SPI_MIN + 1 )
#define PIN_I2C_COUNT       ( PIN_I2C_MAX - PIN_I2C_BASE )
#define PIN_SPI_COUNT       ( PIN_SPI_MAX - PIN_SPI_BASE )
#define PIN_IS_I2CEXP(n)    ( (n) > PIN_I2C_MIN && (n) < PIN_I2C_MAX )
#define PIN_IS_SPIEXP(n)    ( (n) > PIN_SPI_MIN && (n) < PIN_SPI_MAX )

esp_err_t gexp_set_level(int pin, bool level);
esp_err_t gexp_get_level(int pin, bool *level, bool sync);
void gpio_table(bool i2c, bool spi);

#ifdef __cplusplus
}
#endif
