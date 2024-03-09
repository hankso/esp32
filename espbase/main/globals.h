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
#define LPCHR(c, n)             { LOOPN(x, (n)) putchar(c); putchar('\n'); }
#define TRYFREE(p)              { if (p) free(p); (p) = NULL; }
#define UNUSED                  __attribute__((unused))
#define PACKED                  __attribute__((packed))
#define FALLTH                  __attribute__((fallthrough))
#ifndef MAX
#define MAX(a, b)               ( (a) > (b) ? (a) : (b) )
#define MIN(a, b)               ( (a) > (b) ? (b) : (a) )
#endif

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


// Utilities (implemented in utils.c)
void msleep(uint32_t ms);
bool strbool(const char *);
char * cast_away_const(const char *);
bool endswith(const char *, const char *);
bool startswith(const char *, const char *);

const char * format_size(size_t, bool);
const char * format_sha256(const uint8_t *, size_t);

void task_info();
void memory_info();
void version_info();
void hardware_info();
void partition_info();

#ifdef __cplusplus
}
#endif
