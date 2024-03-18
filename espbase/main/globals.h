/* 
 * File: globals.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:51:08
 *
 * Global variables are declared as extern in this header file.
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
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
#define LPCHR(c, n)             { LOOPN(x, (n)) putchar(c); }
#define LPCHRN(c, n)            { LPCHR(c, n); putchar('\n'); }
#define TRYFREE(p)              { if (p) free(p); (p) = NULL; }
#define UNUSED                  __attribute__((unused))
#define PACKED                  __attribute__((packed))
#define FALLTH                  __attribute__((fallthrough))
#ifndef MAX
#define MAX(a, b)               ( (a) > (b) ? (a) : (b) )
#define MIN(a, b)               ( (a) > (b) ? (b) : (a) )
#endif
#define EALLOC(v, l)            \
    ( ((v) = (typeof (v)) malloc(l)) ? ESP_OK : ESP_ERR_NO_MEM )

// Aliases

#if defined(CONFIG_IDF_TARGET_ESP32)
#   define TARGET_ESP32
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#   define TARGET_ESP32S
#   define TARGET_ESP32S2
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#   define TARGET_ESP32S
#   define TARGET_ESP32S3
#else
#warning "This project has only been tested on ESP32 & ESP32-Sn chips"
#endif

// Hotfix for board specified MACROs
#define BOARD_ESP32S3_LUATOS

#ifdef BOARD_ESP32S3_LUATOS
#   undef   CONFIG_GPIO_LED
#   define  CONFIG_GPIO_LED 10
#   undef   CONFIG_USE_I2C1
#   undef   CONFIG_I2C_NUM
#   define  CONFIG_I2C_NUM 1
#   undef   CONFIG_GPIO_TXD
#   define  CONFIG_GPIO_TXD 15
#   undef   CONFIG_GPIO_RXD
#   define  CONFIG_GPIO_RXD 16
#endif
#ifdef BOARD_ESP32S3_NOLOGO
    // TODO
#endif

// Utilities (implemented in utils.c)
void msleep(uint32_t ms);
void asleep(uint32_t ms);
bool strbool(const char *);
void hexdump(const void *src, size_t bytes, size_t maxlen);
char * hexdumps(const void *src, char *dst, size_t bytes, size_t maxlen);
bool endswith(const char *, const char *tail);
bool startswith(const char *, const char *head);
bool parse_int(const char *, int *ptr);
bool parse_uint16(const char *, uint16_t *ptr);
bool parse_float(const char *, float *ptr);
size_t parse_all(const char *, int *arr, size_t arrlen);
char * cast_away_const(const char *);

const char * format_size(size_t, bool);
const char * format_sha256(const void *, size_t);

void task_info(uint8_t sort);
void memory_info();
void version_info();
void hardware_info();
void partition_info();

#ifdef __cplusplus
}
#endif
