/* 
 * File: globals.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:51:08
 */

#pragma once

#include <math.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_idf_version.h"

#undef CONFIG_BASE_USE_WIFI

// GCC tricks

#define UNUSED              __attribute__((unused))
#define PACKED              __attribute__((packed))
#define FALLTH              __attribute__((fallthrough))
#define NOTUSED(x)          (void)(x)
#define CONCAT_(a, b)       a ## b
#define CONCAT(a, b)        CONCAT_(a, b)
#define STR_(x)             #x
#define STR(x)              STR_(x)
#define CASESTR(x, l)       case x: return #x + l
#define CASESTRV(v, x, l)   case x: (v) = #x + l; break
#define SIZEOF(s, a)        sizeof(((s *)0)->a)
#define LEN(arr)            ( sizeof(arr) / sizeof(*arr) )
#define LOOP(x, l, h)       for (typeof(h) x = (l); x < (h); x++)
#define LOOPD(x, h, l)      for (typeof(h) x = (h); x > (l); x--)
#define LOOPN(x, n)         LOOP(x, 0, (n))
#define LOOPND(x, n)        LOOPD(x, (n) - 1, -1)
#define LPCHR(c, n)         do { LOOPN(x, (n)) putchar(c); } while (0)
#define LPCHRN(c, n)        do { LPCHR((c), (n)); putchar('\n'); } while (0)
#define TRYNULL(p, f)       do { if (p) f(p); (p) = (typeof(p))0; } while (0)
#define TRYFREE(p)          TRYNULL((p), free)

// ESP specific

#define TIMEOUT(m)                                                          \
        ( (m) == (typeof(m))(-1) ? portMAX_DELAY : pdMS_TO_TICKS(m) )
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
#define ITERPN(v, a, n)                                                     \
        for (typeof(*(a)) *v = (a), *_##v = v + (n); v < _##v; v++)
#define ITERVN(v, a, n)                                                     \
        for (typeof(*(a)) *_##v = (a), v = *_##v; _##v < (a) + (n); v = *++_##v)
#define ITERP(v, a)         ITERPN(v, (a), LEN((a)))
#define ITERV(v, a)         ITERVN(v, (a), LEN((a)))
#define MUTEX()             xSemaphoreCreateBinary()
#define DMUTEX(s)           TRYNULL((s), vSemaphoreDelete)
#define ACQUIRE(s, t)       ( (s) ? xSemaphoreTake((s), TIMEOUT(t)) : 0 )
#define RELEASE(s)          ( (s) ? xSemaphoreGive(s) : 0 )
#define REGEVTS(base, ...)  esp_event_handler_instance_register(            \
                                base##_EVENT, ESP_EVENT_ANY_ID, __VA_ARGS__)
#define UREGEVTS(base, inst)                                                \
        do {                                                                \
            if (inst) esp_event_handler_instance_unregister(                \
                base##_EVENT, ESP_EVENT_ANY_ID, (inst));                    \
            (inst) = NULL;                                                  \
        } while (0)

// May be defined somewhere

#ifndef BIT
#   define BIT(n)           ( 1UL << (n) )
#endif

#ifndef ABS
#   define ABS(x)           ({ typeof(x) X_ = (x); X_ < 0 ? -X_ : X_; })
#endif

#ifndef ABSDIFF
#   define ABSDIFF(a, b)    ({ typeof(a) A_ = (a), B_ = (b);                \
                               A_ > B_ ? A_ - B_ : B_ - A_; })
#endif

#ifndef MOD
#   define MOD(a, b)        ({ typeof(a) A_ = (a), B_ = (b), M_ = A_ % B_;  \
                               M_ < 0 ? M_ + B_ : M_; })
#endif

#ifndef CDIV
#   define CDIV(a, b)       ({ typeof(a) A_ = (a), B_ = (b);                \
                               B_ ? (A_ + B_ - 1) / B_ : 0; })
#endif

#ifndef MAX
#   define MAX(a, b)        ({ typeof(a) A_ = (a), B_ = (b);                \
                               A_ > B_ ? A_ : B_; })
#   define MIN(a, b)        ({ typeof(a) A_ = (a), B_ = (b);                \
                               A_ > B_ ? B_ : A_; })
#endif

#ifndef CONS
#   define CONS(x, l, h)    MAX((l), MIN((x), (h)))
#endif

#ifndef STATIC_ASSERT
#   define STATIC_ASSERT(c) extern char CONCAT(p, __LINE__)[(c) ? 1 : -1]
#endif

#ifndef bitread
#   define bitsread(v, o, m)    ( ((v) >> (o)) & (m) )
#   define bitnread(v, o, n)    bitsread((v), (o), BIT(n) - 1)
#   define bitread(v, o)        bitsread((v), (o), 1)
#endif

// Version aliases

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#   define IDF_TARGET_V5
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#   define IDF_TARGET_V4
#else
#   warning "This project has only been tested on ESP-IDF v4.4 & v5.x"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// implemented in utils.c
void msleep(uint32_t ms);
uint64_t asleep(uint32_t ms, uint64_t state);

int stridx(const char *str, const char *tpl);
bool strbool(const char *str);
size_t strcnt(const char *str, const char *wants, size_t slen);
char * strtrim(char *str, const char *chars);

char * b64encode(const char *src, char *dst, size_t slen);

bool endswith(const char *, const char *tail);
bool startswith(const char *, const char *head);

bool parse_f64(const char *, double *val);
bool parse_f32(const char *, float *val);
bool parse_s64(const char *, int64_t *val);
bool parse_s32(const char *, int32_t *val);
bool parse_u32(const char *, uint32_t *val);
bool parse_u16(const char *, uint16_t *val);
bool parse_u8(const char *, uint8_t *val);
size_t parse_all(const char *, int *arr, size_t arrlen);
size_t parse_pin(const char *, int *arr, size_t arrlen, const char **names);

void hexdump(const void *src, size_t bytes, size_t maxlen);
void hexdumpl(const void *src, size_t bytes, size_t maxlen);
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
esp_err_t unicode_tricks(const unicode_trick_t *);

const char * unicode2str(uint32_t);
size_t str2unicode(const char *, uint32_t *);

// Must provide file descriptor to `gbktable.bin` which is a 47KB
// mapping file between 16Bit chinese unicode and GBK index.
const char * unicode2gbk(FILE *, uint32_t);
size_t gbk2unicode(FILE *, const char *, uint32_t *);
size_t gbk2str_r(const char *src, char *dst, size_t dlen);
char * gbk2str(const char *src);

const char * format_size(double bytes);
const char * format_sha256(const void *buf, size_t len);
const char * format_binary(uint64_t val, size_t maxbits);

void * setTimeout(uint32_t ms, void (*func)(void *), void *arg);
void * setInterval(uint32_t ms, void (*func)(void *), void *arg);
void clearTimer(void *hdl); // = clearTimeout + clearInterval

bool notify_increase(void *task);
bool notify_decrease(void *task);
bool notify_wait_for(uint32_t value, uint32_t tout_ms, uint32_t wait_ms);

void task_info(uint8_t sort);
void memory_info();
void version_info();
void hardware_info();
void partition_info();

#ifdef __cplusplus
}
#endif
