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
    for (uint8_t i = 0; i < 32; i++) {
        buf[2 * i] = hexdigits(src[i] >> 4);
        buf[2 * i + 1] = hexdigits(src[i] & 0xF);
    }
    buf[MIN(len, 64)] = '\0';
    return buf;
}

const char * format_mac(const uint8_t *src, size_t len) {
    static char buf[17 + 1]; // XX:XX:XX:XX:XX:XX
    if (!src || !len) { buf[0] = '\0'; return buf; }
    for (uint8_t i = 0; i < 6; i++) {
        buf[3 * i] = hexdigits(src[i] >> 4);
        buf[3 * i + 1] = hexdigits(src[i] & 0xF);
        buf[3 * i + 2] = (i == 5) ? '\0' : ':';
    }
    buf[MIN(len, 17)] = '\0';
    return buf;
}

const char * format_ip(uint32_t addr, size_t len) {
    static char buf[15 + 1]; // XXX.XXX.XXX.XXX
    if (!len) { buf[0] = '\0'; return buf; }
    for (uint8_t idx = 0, i = 0; i < 4; i++) {
        idx += snprintf(buf + idx, 16 - idx, "%d", (addr >> (i * 8)) & 0xFF);
        buf[idx++] = (i == 3) ? '\0' : '.';
    }
    buf[MIN(len, 15)] = '\0';
    return buf;
}

// Note: format_size format string into static buffer. Therefore you don't
// need to free the buffer after logging/strcpy etc. But you must save the
// result before calling format_size once again, because the buffer will be
// reused and overwriten.
const char * format_size(size_t bytes, bool inbit) {
    static char buffer[16]; // xxxx.xxu\0
    static char * units[] = { "", "K", "M", "G", "T", "P" };
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
    static const char task_states[] = "*RBSD";
    uint32_t ulTotalRunTime;
    uint16_t num = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = pvPortMalloc(num * sizeof(TaskStatus_t));
    if (tasks == NULL) {
        printf("Cannot allocate space for tasks list");
        return;
    }
    num = uxTaskGetSystemState(tasks, num, &ulTotalRunTime);
    if (!num || !ulTotalRunTime) {
        printf("TaskStatus_t array size too small. Skip");
        vPortFree(tasks);
        return;
    }
    printf("TID State" " Name\t\t" "Pri CPU%%" "Counter Stack\n");
    LOOPN(i, num) {
        printf("%3d (%c)\t " " %s\t\t" "%3d %4.1f" "%7lu %5.5s\n",
               tasks[i].xTaskNumber, task_states[tasks[i].eCurrentState],
               tasks[i].pcTaskName, tasks[i].uxCurrentPriority,
               100.0 * tasks[i].ulRunTimeCounter / ulTotalRunTime,
               tasks[i].ulRunTimeCounter,
               format_size(tasks[i].usStackHighWaterMark, false));
    }
    vPortFree(tasks);
#else
    printf("Unsupported command! Enable `CONFIG_FREERTOS_USE_TRACE_FACILITY` "
           "in menuconfig/sdkconfig to run this command\n");
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

static esp_partition_type_t partition_types[] = {
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_TYPE_APP
};

static void partition_type_str(
    esp_partition_type_t type, esp_partition_subtype_t subtype,
    const char **type_str, const char **subtype_str
) {
    static char type_buf[16], subtype_buf[16];
    snprintf(type_buf, sizeof(type_buf), "0x%02X", type);
    snprintf(subtype_buf, sizeof(subtype_buf), "0x%02X", subtype);
    *type_str = type_buf; *subtype_str = subtype_buf;
    if (type == ESP_PARTITION_TYPE_DATA) {
        *type_str = "data";
        switch (subtype) {
        case ESP_PARTITION_SUBTYPE_DATA_OTA: *subtype_str = "ota"; break;
        case ESP_PARTITION_SUBTYPE_DATA_PHY: *subtype_str = "phy"; break;
        case ESP_PARTITION_SUBTYPE_DATA_NVS: *subtype_str = "nvs"; break;
        case ESP_PARTITION_SUBTYPE_DATA_COREDUMP: *subtype_str = "coredump"; break;
        case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS: *subtype_str = "nvs_keys"; break;
        case ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM: *subtype_str = "efuse_em"; break;
        case ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD: *subtype_str = "esphttpd"; break;
        case ESP_PARTITION_SUBTYPE_DATA_FAT: *subtype_str = "fat"; break;
        case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: *subtype_str = "spiffs"; break;
        default: break;
        }
    } else if (type == ESP_PARTITION_TYPE_APP) {
        *type_str = "app";
        switch (subtype) {
        case ESP_PARTITION_SUBTYPE_APP_FACTORY: *subtype_str = "factory"; break;
        case ESP_PARTITION_SUBTYPE_APP_TEST:    *subtype_str = "test"; break;
        default:
            if (
                subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX
            ) {
                snprintf(subtype_buf, sizeof(subtype_buf), "ota_%d",
                        subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN);
            }
            break;
        }
    }
}

void partition_info() {
    uint8_t idx, num = 0;
    const char *tstr, *ststr;
    const esp_partition_t * parts[16], *part, *tmp;
    esp_partition_iterator_t iter;
    LOOPN(i, LEN(partition_types)) {
        iter = esp_partition_find(
            partition_types[i],
            ESP_PARTITION_SUBTYPE_ANY, NULL);
        while (iter != NULL && num < 16) {
            part = esp_partition_get(iter);
            for (idx = 0; idx < num; idx++) {
                if (parts[idx]->address < part->address) {
                    tmp = parts[idx]; parts[idx] = part; part = tmp;
                }
            }
            parts[num++] = part;
            iter = esp_partition_next(iter);
        }
        esp_partition_iterator_release(iter);
    }
    if (!num) {
        printf("No partitons found in flash. Skip");
        return;
    }
    printf("LabelName    Type SubType  Offset   Size     Secure\n");
    while (num--) {
        part = parts[num];
        partition_type_str(part->type, part->subtype, &tstr, &ststr);
        printf("%-12s %-4s %-8s 0x%06X 0x%06X %s\n",
               part->label, tstr, ststr, part->address, part->size,
               part->encrypted ? "true" : "false");
    }
}
