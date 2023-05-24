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

#ifndef CONFIG_GPIO_I2C_SDA
#define CONFIG_GPIO_I2C_SDA     21
#define CONFIG_GPIO_I2C_SCL     22
#define CONFIG_GPIO_SERVOH      12
#define CONFIG_GPIO_SERVOV      13
#endif

#ifndef CONFIG_GPIO_HSPI_MISO
#define CONFIG_GPIO_HSPI_MISO   12
#define CONFIG_GPIO_HSPI_MOSI   13
#define CONFIG_GPIO_HSPI_SCLK   14
#define CONFIG_GPIO_HSPI_CS0    15
#define CONFIG_GPIO_HSPI_CS1    22
#define CONFIG_GPIO_HSPI_SDCD   4
#endif

#define CONFIG_I2C_NUM          0
#define CONFIG_UART_NUM         0
#define CONFIG_DEBUG

// make it compatiable with Arduino
#ifndef bitRead
#define bitRead(v, b)       ((v) & BIT(b))
#define bitSet(v, b)        ((v) |= BIT(b))
#define bitClear(v, b)      ((v) &= ~BIT(b))
#define bitWrite(v, b, l)   (l ? bitSet(v, b) : bitClear(v, b))
#endif // bitRead


// Utilities (implemented in utils.c)
bool strbool(const char *);
char * cast_away_const(const char*);
char * cast_away_const_force(const char*);

const char * format_sha256(const uint8_t*, size_t);
const char * format_size(size_t);
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
