/* 
 * File: globals.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:51:08
 *
 * Global variables are declared as extern in this header file.
 */

#ifndef _GLOBALS_H_
#define _GLOBALS_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_I2C_NUM          0
#define CONFIG_UART_NUM         0
#define CONFIG_FFS_MP           "/flashfs"  // mount point for flash file system
// #define CONFIG_SDFS_MP          "/sdcard"   // mount point for external SDCard

#define CONFIG_DEBUG
#define CONFIG_AUTO_ALIGN
#define CONFIG_ADC_INPUT                    // adc_read: 4.6KB
#define CONFIG_I2C_SCREEN                   // scn_progbar: 4.0KB
#define CONFIG_ALS_TRACK                    // als_tracking: 1.0KB
#define CONFIG_PWM_SERVO                    // pwm_xxx_degree: 1.5KB
#define CONFIG_PWM_BUZZER                   // pwm_xxx_tone: 1.1KB
#define CONFIG_LED_INDICATOR                // led_xxx: 27.4KB
// #define CONFIG_KNOB_INPUT                   // knob_input: 2.3KB
// #define CONFIG_VLX_SENSOR                   // vlx_probe: 5.0KB
// #define CONFIG_I2C_GPIOEXP                  // i2c_gpio_xxx: 3.0KB
// #define CONFIG_SPI_GPIOEXP                  // spi_gpio_xxx: 16KB
// #define CONFIG_OTA_FETCH                    // ota_updation_url: 263KB

#ifndef CONFIG_GPIO_LED
#define CONFIG_GPIO_LED         5
#endif

#ifndef CONFIG_LED_NUM
#define CONFIG_LED_NUM          1
#define CONFIG_LED_MODE_GPIO
// #define CONFIG_LED_MODE_LEDC
// #define CONFIG_LED_MODE_RMT
#endif

#ifndef CONFIG_GPIO_HSPI_MISO
#define CONFIG_GPIO_HSPI_MISO   12
#define CONFIG_GPIO_HSPI_MOSI   13
#define CONFIG_GPIO_HSPI_SCLK   14
#define CONFIG_GPIO_HSPI_CS0    15
#define CONFIG_GPIO_HSPI_CS1    27
#endif

#ifndef CONFIG_GPIO_SCN_SDA
#define CONFIG_GPIO_SCN_SDA     18
#define CONFIG_GPIO_SCN_SCL     19
#endif

#ifndef CONFIG_GPIO_I2C_SDA
#define CONFIG_GPIO_I2C_SDA     25
#define CONFIG_GPIO_I2C_SCL     26
#endif

#ifndef CONFIG_GPIO_SERVOV
#define CONFIG_GPIO_SERVOV      32
#define CONFIG_GPIO_SERVOH      33
#define CONFIG_GPIO_BUZZER      4
#endif

#ifndef CONFIG_GPIO_BTN
#define CONFIG_GPIO_BTN         36
#define CONFIG_GPIO_ENCA        37
#define CONFIG_GPIO_ENCB        38
#define CONFIG_GPIO_INT         39
#endif


#define _STR_IMPL_(x)           #x
#define STR(x)                  _STR_IMPL_(x)
#define CASESTR(x, n)           case x: return #x + n;
#define NOTUSED(x)              (void)(x)
#define ABSDIFF(a, b)           ( (a) > (b) ? ((a) - (b)) : ((b) - (a)) )
#define LEN(arr)                ( sizeof(arr) / sizeof(*arr) )
#define LOOP(x, l, h)           for (int (x) = (l); (x) < (h); (x)++)
#define LOOPD(x, h, l)          for (int (x) = (h); (x) > (l); (x)--)
#define LOOPN(x, n)             LOOP(x, 0, (n))
#define LOOPND(x, n)            LOOPD(x, (n) - 1, -1)
#define LPCHR(c, n)             { LOOPN(x, (n)) putchar(c); putchar('\n'); }
#define TRYFREE(p)              { if (p) free(p); (p) = NULL; }
#define UNUSED                  __attribute__((unused))
#define PACKED                  __attribute__((packed))
#define FALLTH                  __attribute__((fallthrough))


// Utilities (implemented in utils.c)
void msleep(uint32_t ms);
bool strbool(const char *);
char * cast_away_const(const char*);
char * cast_away_const_force(const char*);

const char * format_sha256(const uint8_t*, size_t);
const char * format_size(size_t, bool);
const char * format_mac(const uint8_t*, size_t);
const char * format_ip(uint32_t, size_t);

void task_info();
void memory_info();
void version_info();
void hardware_info();
void partition_info();

#ifdef __cplusplus
}
#endif

#endif // _GLOBALS_H_
