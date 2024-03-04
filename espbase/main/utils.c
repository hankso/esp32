/* 
 * File: utils.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-20 21:37:14
 */

#include "globals.h"
#include "config.h"

#include "sys/param.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void msleep(uint32_t ms) { vTaskDelay(ms / portTICK_PERIOD_MS); }

char * cast_away_const(const char *str) {
    return strdup(str);
}

char * cast_away_const_force(const char *str) {
    return (char *)(void *)(const void *)str;
}

bool strbool(const char *str) {
    if (!str) return false;
    return !strcmp(str, "1") || !strcmp(str, "y") || !strcmp(str, "on");
}

char hexdigits(uint8_t v) {
    return (v < 10) ? (v + '0') : (v - 10 + 'a');
}

const char * format_sha256(const uint8_t *src, size_t len) {
    static char buf[64 + 1];
    if (!src || !len) { buf[0] = '\0'; return buf; }
    LOOPN(i, 32) {
        buf[2 * i] = hexdigits(src[i] >> 4);
        buf[2 * i + 1] = hexdigits(src[i] & 0xF);
    }
    buf[MIN(len, 64)] = '\0';
    return buf;
}

// Note: format_size format string into static buffer. Therefore you don't
// need to free the buffer after logging/strcpy etc. But you must save the
// result before calling format_size once again, because the buffer will be
// reused and overwriten.
const char * format_size(size_t bytes, bool inbit) {
    static char buffer[16]; // xxxx.xxu\0
    static char * units[] = { " ", "K", "M", "G", "T", "P" };
    static int Bdems[] = { 0, 1, 2, 3, 3, 4 };
    static int bdems[] = { 0, 2, 3, 3, 4, 7 };
    if (!bytes)
        return inbit ? "0 b" : "0 B";
    double tmp = bytes * (inbit ? 8 : 1), base = 1024;
    int exp = 0;                        // you can replace this with log10
    while (exp < 5 && tmp > base) {
        tmp /= base;
        exp++;
    }
    snprintf(buffer, sizeof(buffer), "%.*f %s%c",
            inbit ? bdems[exp] : Bdems[exp], tmp, units[exp], "Bb"[inbit]);
    return buffer;
}

void task_info() {
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
    // Running Ready Blocked Suspended Deleted
    static const char task_states[] = "*RBSD", *taskname;
    uint32_t ulTotalRunTime;
    uint16_t num = uxTaskGetNumberOfTasks();
    TaskStatus_t tmp, *tasks = pvPortMalloc(num * sizeof(TaskStatus_t));
    if (tasks == NULL) {
        printf("Could not allocate space for tasks list");
        return;
    }
    if (!( num = uxTaskGetSystemState(tasks, num, &ulTotalRunTime) )) {
        printf("TaskStatus_t array size too small. Skip");
        vPortFree(tasks);
        return;
    }
    // Sort tasks by pcTaskName and xCoreID
    LOOPN(i, num) {
        if (tasks[i].xCoreID > 1) tasks[i].xCoreID = -1;
        LOOP(j, i + 1, num) {
            if (strcmp(tasks[i].pcTaskName, tasks[j].pcTaskName) > 0) {
                tmp = tasks[i];
                tasks[i] = tasks[j];
                tasks[j] = tmp;
            }
        }
    }
#   ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    LOOPN(i, num) { ulTotalRunTime += tasks[i].ulRunTimeCounter; }
#   endif
    printf("TID State Name            Pri CPU Usage%% StackHW\n");
    LOOPN(i, num) {
        if (!strcmp(taskname = tasks[i].pcTaskName, "IDLE"))
            taskname = tasks[i].xCoreID ? "CPU 1 App" : "CPU 0 Pro";
        printf("%3d  (%c)  %-15s %2d  %3d %5.1f  %7s\n",
               tasks[i].xTaskNumber, task_states[tasks[i].eCurrentState],
               taskname, tasks[i].uxCurrentPriority, tasks[i].xCoreID,
               100.0 * tasks[i].ulRunTimeCounter / ulTotalRunTime,
               format_size(tasks[i].usStackHighWaterMark, false));
    }
    vPortFree(tasks);
#else
    puts("Unsupported command! Enable `CONFIG_FREERTOS_USE_TRACE_FACILITY` "
         "in menuconfig/sdkconfig to run this command");
#endif
}

void version_info() {
    const esp_app_desc_t *desc = esp_ota_get_app_description();
    printf("IDF Version: %s based on FreeRTOS %s\n"
           "Firmware Version: %s %s\n"
           "Compile time: %s %s\n",
           esp_get_idf_version(), tskKERNEL_VERSION_NUMBER,
           desc->project_name, desc->version, __DATE__, __TIME__);
}

static uint16_t memory_types[] = {
    0, MALLOC_CAP_SPIRAM, MALLOC_CAP_EXEC, MALLOC_CAP_DMA,
    MALLOC_CAP_INTERNAL, MALLOC_CAP_DEFAULT
};

static const char * const memory_names[] = {
    "TOTAL", "SPI RAM", "EXEC", "DMA", "INTERN", "DEFAULT"
};

void memory_info() {
    uint16_t mem_type = 0;
    size_t total_size;
    multi_heap_info_t info;
    printf("Type        Size     Used    Avail Used%%\n");
    LOOPND(i, LEN(memory_types)) {
        heap_caps_get_info(&info, i ? memory_types[i] : mem_type);
        total_size = info.total_free_bytes + info.total_allocated_bytes;
        printf("%-7s %8s %8s %8s %5.1f\n",
            memory_names[i], format_size(total_size, false),
            format_size(info.total_allocated_bytes, false),
            format_size(info.total_free_bytes, false),
            100.0 * info.total_allocated_bytes / total_size);
        if (total_size)
            mem_type |= memory_types[i];
    }
}

void hardware_info() {
    esp_chip_info_t info;
    esp_chip_info(&info);
    printf(
        "Chip UID: %s-%s\n"
        "  Model: %s\n"
        "  Cores: %d\n"
        "Revision: %d\n"
        "  Feature: %s %s flash%s%s%s\n",
        Config.info.NAME, Config.info.UID,
        info.model == CHIP_ESP32 ? "ESP32" : "???",
        info.cores, info.revision,
        format_size(spi_flash_get_chip_size(), false),
        info.features & CHIP_FEATURE_EMB_FLASH ? "Embedded" : "External",
        info.features & CHIP_FEATURE_WIFI_BGN ? " | WiFi 802.11bgn" : "",
        info.features & CHIP_FEATURE_BLE ? " | BLE" : "",
        info.features & CHIP_FEATURE_BT ? " | BT" : ""
    );

    uint8_t sta[6], ap[6];
    esp_read_mac(sta, ESP_MAC_WIFI_STA);
    esp_read_mac(ap, ESP_MAC_WIFI_SOFTAP);
    printf("STA MAC address: " MACSTR "\n", MAC2STR(sta));
    printf("AP  MAC address: " MACSTR "\n", MAC2STR(ap));
}

static const char * partition_subtype_str(
    esp_partition_type_t type, esp_partition_subtype_t subtype
) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", subtype);
    if (type == ESP_PARTITION_TYPE_DATA) {
        switch (subtype) {
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_OTA, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_PHY, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_NVS, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_COREDUMP, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_FAT, 27);
            CASESTR(ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 27);
            default: break;
        }
    } else if (type == ESP_PARTITION_TYPE_APP) {
        switch (subtype) {
            CASESTR(ESP_PARTITION_SUBTYPE_APP_FACTORY, 26);
            CASESTR(ESP_PARTITION_SUBTYPE_APP_TEST, 26);
            default:
                if (
                    subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                    subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX
                ) {
                    subtype -= ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
                    snprintf(buf, sizeof(buf), "ota_%d", subtype);
                }
                break;
        }
    }
    return buf;
}

static const char * partition_type_str(esp_partition_type_t type) {
    static char buf[8];
    switch (type) {
        CASESTR(ESP_PARTITION_TYPE_DATA, 19);
        CASESTR(ESP_PARTITION_TYPE_APP, 19);
        default: snprintf(buf, sizeof(buf), "0x%02X", type); return buf;
    }
}

void partition_info() {
    uint8_t num = 0;
    const esp_partition_t * parts[16], *part, *tmp;
    esp_partition_iterator_t iter = esp_partition_find(
        ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (iter != NULL && num < LEN(parts)) {
        part = esp_partition_get(iter);
        LOOPN(j, num) {
            if (parts[j]->address < part->address) {
                tmp = parts[j]; parts[j] = part; part = tmp;
            }
        }
        parts[num++] = part;
        iter = esp_partition_next(iter);
    }
    esp_partition_iterator_release(iter);
    if (!num) {
        printf("No partitons found in flash. Skip");
        return;
    }
    printf("LabelName    Type SubType  Offset   Size     Secure\n");
    while (num--) {
        part = parts[num];
        printf("%-12s %-4s %-8s 0x%06X 0x%06X %s\n",
               part->label, partition_type_str(part->type),
               partition_subtype_str(part->type, part->subtype),
               part->address, part->size, part->encrypted ? "true" : "false");
    }
}
