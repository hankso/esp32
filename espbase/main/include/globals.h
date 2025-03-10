/* 
 * File: globals.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:51:08
 */

#pragma once

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "soc/soc_caps.h"

#define UNUSED                  __attribute__((unused))
#define PACKED                  __attribute__((packed))
#define FALLTH                  __attribute__((fallthrough))
#define NOTUSED(x)              (void)(x)
#define _STR_IMPL_(x)           #x
#define STR(x)                  _STR_IMPL_(x)
#define CASESTR(x, offset)      case x: return #x + offset
#define CASESTRV(v, x, offset)  case x: (v) = #x + offset; break
#define LEN(arr)                ( sizeof(arr) / sizeof(*arr) )
#define LOOP(x, low, high)      for (int x = (low); x < (high); x++)
#define LOOPD(x, high, low)     for (int x = (high); x > (low); x--)
#define LOOPN(x, n)             LOOP(x, 0, (n))
#define LOOPND(x, n)            LOOPD(x, (n) - 1, -1)
#define LPCHR(c, n)             do { LOOPN(x, (n)) putchar(c); } while (0)
#define LPCHRN(c, n)            do { LPCHR((c), (n)); putchar('\n'); } while (0)
#define TRYNULL(p, f)           do { if (p) f(p); (p) = NULL; } while (0)
#define TRYFREE(p)              TRYNULL((p), free)

#define TIMEOUT(m)              ( (m) > 0 ? pdMS_TO_TICKS(m) : portMAX_DELAY )
#define EMALLOC(v, size)                                                    \
        ( (v = (typeof(v)) malloc(size)) ? ESP_OK : ESP_ERR_NO_MEM )
#define ECALLOC(v, num, size)                                               \
        ( (v = (typeof(v)) calloc((num), (size))) ? ESP_OK : ESP_ERR_NO_MEM )
#define EREALLOC(v, size)                                                   \
        ({                                                                  \
            typeof(v) ptr = (typeof(v)) realloc((v), (size));               \
            if (ptr) (v) = ptr;                                             \
            ptr ? ESP_OK : ESP_ERR_NO_MEM;                                  \
        })
#define ITERN(v, arr, n)                                                    \
        for (typeof(*(arr)) *_p = (arr), v = *_p; _p < &((arr)[n]); v = *++_p)
#define ITER(v, arr) ITERN(v, (arr), LEN(arr))

#ifndef BIT
#   define BIT(n)               ( 1UL << (n) )
#endif
#ifndef ABS
#   define ABS(x)               ( (x) > 0 ? (x) : -(x) )
#endif
#ifndef ABSDIFF
#   define ABSDIFF(a, b)        ( (a) > (b) ? (a) - (b) : (b) - (a) )
#endif
#ifndef MAX
#   define MAX(a, b)            ( (a) > (b) ? (a) : (b) )
#   define MIN(a, b)            ( (a) > (b) ? (b) : (a) )
#endif
#ifndef CONS
#   define CONS(x, low, high)   MAX((low), MIN((x), (high)))
#endif

// Aliases

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#   define TARGET_IDF_5
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#   define TARGET_IDF_4
#else
#   warning "This project has only been tested on ESP-IDF v4.4 & v5.x"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32)
#   define TARGET_ESP32
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#   define TARGET_ESP32S
#   define TARGET_ESP32S2
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#   define TARGET_ESP32S
#   define TARGET_ESP32S3
#else
#   warning "This project has only been tested on ESP32 & ESP32-S chips"
#endif

// Board specified configs

#ifdef TARGET_ESP32S3
// #   define  BOARD_ESP32S3_LUATOS
#   define  BOARD_ESP32S3_NOLOGO
#else
// #   define  BOARD_ESP32_DEVKIT
#   define  BOARD_ESP32_PICOKIT
#endif

#if defined(BOARD_ESP32_DEVKIT)
#   undef   CONFIG_BASE_GPIO_LED
#   define  CONFIG_BASE_GPIO_LED 2
#elif defined(BOARD_ESP32_PICOKIT)
#   undef   CONFIG_BASE_LED_MODE_GPIO
#   undef   CONFIG_BASE_LED_MODE_LEDC
#   define  CONFIG_BASE_LED_MODE_RMT
#   undef   CONFIG_BASE_LED_NUM
#   define  CONFIG_BASE_LED_NUM 8
#elif defined(BOARD_ESP32S3_LUATOS)
#   undef   CONFIG_BASE_GPIO_LED
#   define  CONFIG_BASE_GPIO_LED 10
// #   undef   CONFIG_BASE_GPIO_TXD
// #   define  CONFIG_BASE_GPIO_TXD 3
// #   undef   CONFIG_BASE_GPIO_RXD
// #   define  CONFIG_BASE_GPIO_RXD 4
#elif defined(BOARD_ESP32S3_NOLOGO)
#   undef   CONFIG_BASE_LED_MODE_GPIO
#   undef   CONFIG_BASE_LED_MODE_LEDC
#   define  CONFIG_BASE_LED_MODE_RMT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// implemented in utils.c
void msleep(uint32_t ms);
uint64_t asleep(uint32_t ms, uint64_t state);

bool strbool(const char *);
char * strtrim(char *str, const char *chars);

char * b64encode(char *out, const char *inp, size_t len);

bool endswith(const char *, const char *tail);
bool startswith(const char *, const char *head);

bool parse_int(const char *, int *ptr);
bool parse_uint16(const char *, uint16_t *ptr);
bool parse_float(const char *, float *ptr);
size_t parse_all(const char *, int *arr, size_t arrlen);

void hexdump(const void *src, size_t bytes, size_t maxlen);
char * hexdumps(const void *src, char *dst, size_t bytes, size_t maxlen);

typedef struct {
    uint8_t index;
#define UANIM_CIRCLE 0
#define UANIM_V_BAR  1
#define UANIM_H_BAR  2
#define UANIM_SHADE  3
#define UANIM_DOT    4
    uint8_t repeat;
    uint16_t timeout_ms;
    FILE *stream;
} unicode_trick_t;

const char * unicode2str(uint32_t);
uint32_t str2unicode(const char *);
esp_err_t unicode_tricks(const unicode_trick_t *);

const char * format_size(uint64_t, bool);
const char * format_sha256(const void *, size_t);
const char * format_binary(uint64_t, size_t);

void * setTimeout(uint32_t ms, void (*func)(void *), void *arg);
void * setInterval(uint32_t ms, void (*func)(void *), void *arg);
void clearTimer(void *hdl); // = clearTimeout + clearInterval

void task_info(uint8_t sort);
void memory_info();
void version_info();
void hardware_info();
void partition_info();

#ifdef __cplusplus
}
#endif
