/* 
 * File: drivers.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-05-06 19:53:43
 */

#pragma once

#include "globals.h"

#include "driver/uart.h"        // for UART_NUM_XXX
#include "driver/gpio.h"        // for GPIO_NUM_XXX
#include "driver/spi_master.h"  // for SPIXXX_HOST
#ifdef IDF_TARGET_V4
#   include "driver/i2c.h"      // for I2C_NUM_XXX
#   include "driver/i2s.h"      // for I2S_NUM_XXX
#else
#   include "driver/i2c_master.h"
#   include "driver/i2s_pdm.h"
#endif

#if defined(CONFIG_BASE_USE_BTN) && !__has_include("iot_button.h")
#   warning "Run `idf.py add-dependency espressif/button`"
#   undef CONFIG_BASE_USE_BTN
#endif

#if defined(CONFIG_BASE_USE_KNOB) && !__has_include("iot_knob.h")
#   warning "Run `idf.py add-dependency espressif/knob`"
#   undef CONFIG_BASE_USE_KNOB
#endif

#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)

#define _SPI_NUMBER(num) SPI##num##_HOST
#define SPI_NUMBER(num) _SPI_NUMBER(num)

#define _I2S_NUMBER(num) I2S_NUM_##num
#define I2S_NUMBER(num) _I2S_NUMBER(num)

#define _UART_NUMBER(num) UART_NUM_##num
#define UART_NUMBER(num) _UART_NUMBER(num)

#define _GPIO_NUMBER(num) GPIO_NUM_##num
#define GPIO_NUMBER(num) _GPIO_NUMBER(num)

#ifdef CONFIG_BASE_USE_LED
#   if defined(CONFIG_BASE_BOARD_ATCAM)
#       define PIN_LED  GPIO_NUMBER(33)
#   elif defined(CONFIG_BASE_BOARD_DEVKIT)
#       define PIN_LED  GPIO_NUMBER(2)
#   elif defined(CONFIG_BASE_BOARD_USBOTG)
#       define PIN_LED  GPIO_NUMBER(15)
#   elif defined(CONFIG_BASE_BOARD_S3LUAT)
#       define PIN_LED  GPIO_NUMBER(10)
#   elif defined(CONFIG_BASE_BOARD_S3ECAM)
#       define PIN_LED  GPIO_NUMBER(21)
#   elif defined(CONFIG_BASE_BOARD_S3XMINI)
#       define PIN_LED  GPIO_NUMBER(48)
#   else
#       define PIN_LED  GPIO_NUMBER(CONFIG_BASE_GPIO_LED)
#   endif
#endif

#ifdef CONFIG_BASE_USE_UART
#   define NUM_UART     UART_NUMBER(CONFIG_BASE_UART_NUM)
#   define PIN_TXD      GPIO_NUMBER(CONFIG_BASE_GPIO_UART_TXD)
#   define PIN_RXD      GPIO_NUMBER(CONFIG_BASE_GPIO_UART_RXD)
#   define PIN_RTS      UART_PIN_NO_CHANGE
#   define PIN_CTS      UART_PIN_NO_CHANGE
#endif

#ifdef CONFIG_BASE_USE_I2S
#   define NUM_I2S      I2S_NUMBER(CONFIG_BASE_I2S_NUM)
#   ifdef CONFIG_BASE_BOARD_S3ECAM
#       define PIN_CLK  GPIO_NUMBER(16)
#       define PIN_DAT  GPIO_NUMBER(17)
#   else
#       define PIN_CLK  GPIO_NUMBER(CONFIG_BASE_GPIO_I2S_CLK)
#       define PIN_DAT  GPIO_NUMBER(CONFIG_BASE_GPIO_I2S_DAT)
#   endif
#endif

#ifdef CONFIG_BASE_USE_I2C
#   if defined(CONFIG_BASE_BOARD_SDA) && CONFIG_BASE_I2C_NUM == 0
#       define PIN_SDA0 GPIO_NUMBER(CONFIG_BASE_BOARD_SDA)
#       define PIN_SCL0 GPIO_NUMBER(CONFIG_BASE_BOARD_SCL)
#   elif defined(CONFIG_BASE_USE_I2C0)
#       define PIN_SDA0 GPIO_NUMBER(CONFIG_BASE_GPIO_I2C_SDA0)
#       define PIN_SCL0 GPIO_NUMBER(CONFIG_BASE_GPIO_I2C_SCL0)
#   endif
#   if defined(CONFIG_BASE_BOARD_SDA) && CONFIG_BASE_I2C_NUM == 1
#       define PIN_SDA1 GPIO_NUMBER(CONFIG_BASE_BOARD_SDA)
#       define PIN_SCL1 GPIO_NUMBER(CONFIG_BASE_BOARD_SCL)
#   elif defined(CONFIG_BASE_USE_I2C1)
#       define PIN_SDA1 GPIO_NUMBER(CONFIG_BASE_GPIO_I2C_SDA1)
#       define PIN_SCL1 GPIO_NUMBER(CONFIG_BASE_GPIO_I2C_SCL1)
#   endif
#   define NUM_I2C      I2C_NUMBER(CONFIG_BASE_I2C_NUM)
#   if CONFIG_BASE_I2C_NUM == 0
#       define PIN_SDA  PIN_SDA0
#       define PIN_SCL  PIN_SCL0
#   else
#       define PIN_SDA  PIN_SDA1
#       define PIN_SCL  PIN_SCL1
#   endif
#endif

#ifdef CONFIG_BASE_USE_SPI
#   define NUM_SPI      SPI_NUMBER(CONFIG_BASE_SPI_NUM)
#   ifdef CONFIG_BASE_BOARD_S3ECAM
#       define PIN_MISO GPIO_NUMBER(5)
#       define PIN_MOSI GPIO_NUMBER(6)
#       define PIN_SCLK GPIO_NUMBER(7)
#   else
#       define PIN_MISO GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_MISO)
#       define PIN_MOSI GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_MOSI)
#       define PIN_SCLK GPIO_NUMBER(CONFIG_BASE_GPIO_SPI_SCLK)
#   endif
#endif

#if defined(CONFIG_BASE_ADC_HALL_SENSOR)
#   define PIN_ADC0     GPIO_NUMBER(36)
#   define PIN_ADC1     GPIO_NUMBER(39)
#elif defined(CONFIG_BASE_ADC_JOYSTICK)
#   define PIN_ADC0     GPIO_NUMBER(CONFIG_BASE_GPIO_ADC0)
#   define PIN_ADC1     GPIO_NUMBER(CONFIG_BASE_GPIO_ADC1)
#elif defined(CONFIG_BASE_USE_ADC)
#   define PIN_ADC0     GPIO_NUMBER(CONFIG_BASE_GPIO_ADC0)
#endif
#if defined(CONFIG_BASE_BOARD_S3NL191)
#   define PIN_ADC2     GPIO_NUMBER(4)
#endif

#ifdef CONFIG_BASE_BTN_INPUT
#   ifdef CONFIG_BASE_BOARD_S3NL191
#       define PIN_BTN  GPIO_NUMBER(14)
#   else
#       define PIN_BTN  GPIO_NUMBER(CONFIG_BASE_GPIO_BTN)
#   endif
#endif

#ifdef CONFIG_BASE_USE_KNOB
#   define PIN_ENCA     GPIO_NUMBER(CONFIG_BASE_GPIO_ENCA)
#   define PIN_ENCB     GPIO_NUMBER(CONFIG_BASE_GPIO_ENCB)
#endif

#ifdef CONFIG_BASE_USE_SERVO
#   define PIN_SVOH     GPIO_NUMBER(CONFIG_BASE_GPIO_SERVOH)
#   define PIN_SVOV     GPIO_NUMBER(CONFIG_BASE_GPIO_SERVOV)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void driver_initialize();

int adc_hall();             // return -1 if error, else positive number of a.u.
int adc_read(uint8_t idx);  // return -1 if error, else measured 0-3300 mV
int adc_joystick(int *dx, int *dy); // return -1 if error, else x << 16 | y

typedef union {
    struct {
        uint8_t offset;     // 0 ~ 255 => -128 ~ 127
        uint8_t phase : 4;  // DAC_CW_PHASE_0 (0x02) or DAC_CW_PHASE_180 (0x03)
        uint8_t scale : 4;  // 0 ~ 3: 1/1, 1/2, 1/4, 1/8 of VDD3P3_RTC
        uint16_t freq;      // 130(130Hz) ~ 55000(100KHz)
    } PACKED;
    uint32_t fspo;          // little endian
} dac_output_t;
esp_err_t dac_write(uint8_t idx, uint8_t val);
esp_err_t dac_cwave(uint8_t idx, uint32_t fspo);

int tpad_read(uint8_t idx); // return -1 if error, else positive number of a.u.

esp_err_t pwm_set_degree(int hdeg, int vdeg);
esp_err_t pwm_get_degree(int *hdeg, int *vdeg);
esp_err_t pwm_set_tone(int freq, int pcent);
esp_err_t pwm_get_tone(int *freq, int *pcent);

#define SMBUS_TO_WORD(reg)  ( (reg) | 0x8000 )
#define SMBUS_IS_WORD(reg)  ( (reg) & 0x8000 || (reg) > 0xFF )
#define SMBUS_HI_WORD(reg)  (( (reg) >> 8 ) & ~0x80 )
#define SMBUS_LO_WORD(reg)  ( (reg) & 0xFF )
esp_err_t smbus_rregs(uint8_t bus, uint8_t addr, uint16_t reg, uint8_t *, size_t);
esp_err_t smbus_wregs(uint8_t bus, uint8_t addr, uint16_t reg, uint8_t *, size_t);
esp_err_t smbus_write_byte(uint8_t bus, uint8_t addr, uint16_t reg, uint8_t val);
esp_err_t smbus_write_word(uint8_t bus, uint8_t addr, uint16_t reg, uint16_t val);
esp_err_t smbus_read_byte(uint8_t bus, uint8_t addr, uint16_t reg, uint8_t *val);
esp_err_t smbus_read_word(uint8_t bus, uint8_t addr, uint16_t reg, uint16_t *val);
esp_err_t smbus_clearbits(uint8_t bus, uint8_t addr, uint16_t reg, uint8_t mask);
esp_err_t smbus_setbits(uint8_t bus, uint8_t addr, uint16_t reg, uint8_t mask);
esp_err_t smbus_toggle(uint8_t bus, uint8_t addr, uint16_t reg, uint8_t bit);
esp_err_t smbus_dump(uint8_t bus, uint8_t addr, uint16_t reg, size_t num);
esp_err_t i2c_wtrd(uint8_t bus, uint8_t addr, void *, size_t, void *, size_t);
esp_err_t i2c_probe(uint8_t bus, uint8_t addr);
uint8_t i2c_detect(uint8_t bus);

#define RT_WBYTE(r, v)      { (0 << 16) | (r), (v) }
#define RT_WWORD(r, v)      { (1 << 16) | (r), (v) }
#define RT_RBYTE(r)         { (2 << 16) | (r), 0 }
#define RT_RWORD(r)         { (3 << 16) | (r), 0 }
#define RT_CBITS(r, m)      { (4 << 16) | (r), (m) }
#define RT_SBITS(r, m)      { (5 << 16) | (r), (m) }
#define RT_TOGGLE(r, b)     { (6 << 16) | (r), (b) }
#define RT_WAIT0(r, m, ms)  { (7 << 16) | (r), (m) | ((ms) << 16) } // none
#define RT_WAIT1(r, m, ms)  { (8 << 16) | (r), (m) | ((ms) << 16) } // any
#define RT_WAIT2(r, m, ms)  { (9 << 16) | (r), (m) | ((ms) << 16) } // all
#define RT_SLEEP(ms)        { 0xFF << 16, (ms) }
#define RT_FIND_OPT(arr, o) ({ smbus_regval_t *p = (arr);                     \
                               while (p && (p->reg >> 16) != (o)) p++; p; })
#define RT_FIND_REG(arr, r) ({ smbus_regval_t *p = (arr);                     \
                               while (p && (p->reg & 0xFFFF) != (r)) p++; p; })
typedef struct {
    uint32_t reg, val;
} smbus_regval_t;
typedef struct {
    uint8_t addr;
    esp_err_t (*init)(uint8_t bus, uint8_t addr);
} smbus_device_t;
esp_err_t smbus_regtable(uint8_t bus, uint8_t addr, smbus_regval_t *, size_t);
uint8_t smbus_detect(uint8_t bus, smbus_device_t *, size_t);

typedef enum {
    // GPIO Expander by PCF8574: Endstops | Temprature | Valves
    PIN_I2C_MIN = GPIO_PIN_COUNT - 1,
#ifdef CONFIG_BASE_GEXP_I2C
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
#ifdef CONFIG_BASE_GEXP_SPI
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
void gexp_table(bool i2c, bool spi);

/* puts(gpio_usage(PIN, NULL) ?: "")    // get usage
 * gpio_usage(PIN, "LED") == "LED"      // set usage
 * gpio_usage(PIN, "LED") != "LED"      // already in use
 */
const char * gpio_usage(gpio_num_t pin, const char *usage);
esp_err_t gpio_reconfig(gpio_num_t pin, const char *desc);

#ifdef __cplusplus
}
#endif
