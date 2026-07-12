/*
 * File: espmisc.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2025/10/11 20:44:45
 */

#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_idf_version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "cJSON.h"

// ESP specific macros

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
#define MSFPO()             pdTICKS_TO_MS(xTaskGetTickCount())
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
#define STRACE(tag, msg)                                                    \
        for (int a, b = uxTaskGetStackHighWaterMark(NULL), done = 0; !done; \
            a = uxTaskGetStackHighWaterMark(NULL),                          \
            done = ({ ESP_LOGI(tag, "%s: %d Bytes", msg, b - a); 1; })      \
        )

// Version aliases

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#   define IDF_TARGET_V6
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#   define IDF_TARGET_V5
#elif ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0)
#   define IDF_TARGET_V4
#else
#   warning "This project has only been tested on ESP-IDF v4.4 & v5.x & v6.0"
#endif

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t errval(int error);
const char *errstr(int error);

typedef struct {
    void * task;
    bool halt;
    uint32_t target_ms;
    uint32_t timeout_ms;
} reboot_ctx_t;

void reboot(void *ctx);

void msleep(uint32_t ms);
TickType_t asleep(uint32_t ms, TickType_t state);

size_t parse_pin(const char *, int *arr, size_t arrlen, const char **names);

size_t gbk2str_r(const char *src, char *dst, size_t dlen);
char * gbk2str(const char *src);

const char * format_size(double bytes);
const char * format_time(double msecs);
const char * format_sha256(const void *buf, size_t len);
const char * format_binary(uint64_t val, size_t maxbits);

typedef struct {
    uint8_t index;
#define UANIM_CIRCLE 0
#define UANIM_V_BAR  1
#define UANIM_H_BAR  2
#define UANIM_SHADE  3
#define UANIM_DOTS   4
    uint8_t repeat;
    uint16_t timeout_ms;
    FILE *stream;
} unicode_trick_t;

esp_err_t unicode_tricks(const unicode_trick_t *);

bool notify_increase(void *task);
bool notify_decrease(void *task);
bool notify_wait_for(uint32_t value, uint32_t tout_ms, uint32_t wait_ms);
#define notify_released(t, w) notify_wait_for(0, (t), (w))

typedef enum {
    TSORT_STATE,
    TSORT_TID,
    TSORT_CPU,
    TSORT_PRI,
    TSORT_NAME,
    TSORT_STACK,
    TSORT_USAGE,
} tsort_t;

void task_info(tsort_t);
void memory_info();
void version_info();
void hardware_info();
void partition_info();

#ifdef __cplusplus
}
#endif
