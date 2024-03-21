/* 
 * File: utils.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-20 21:37:14
 */

#include "globals.h"
#include "filesys.h"
#include "config.h"

#include "nvs.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "soc/efuse_periph.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void msleep(uint32_t ms) { vTaskDelay(ms / portTICK_PERIOD_MS); }

void asleep(uint32_t ms) {
    static TickType_t tick_curr = 0, tick_next = 0;
    tick_curr = xTaskGetTickCount(); // Accurate time control
    if (!tick_next) {
        tick_next = tick_curr;
    } else if (tick_curr < tick_next) {
        vTaskDelay(tick_next - tick_curr);
    }
    tick_next += ms * portTICK_PERIOD_MS;
}

bool strbool(const char *str) {
    if (!str) return false;
    return !strcmp(str, "1") || !strcmp(str, "y") || !strcmp(str, "on");
}

bool endswith(const char *str, const char *tail) {
    if (!str || !tail) return false;
    size_t len = MAX(strlen(str) - strlen(tail), 0);
    return !strcmp(str + len, tail);
}

bool startswith(const char *str, const char *head) {
    if (!str || !head || strlen(str) < strlen(head)) return false;
    return !strncmp(str, head, strlen(head));
}

bool parse_int(const char *str, int *var) {
    if (str == NULL) return false;
    char *endptr;
    int val = strtol(str, &endptr, 0);
    if (endptr == str) return false;
    if (var != NULL) *var = val;
    return true;
}

bool parse_uint16(const char *str, uint16_t *var) {
    int value;
    if (!parse_int(str, &value)) return false;
    if (value < 0 || value > (uint16_t)-1) return false;
    if (var != NULL) *var = value;
    return true;
}

bool parse_float(const char *str, float *var) {
    if (str == NULL) return false;
    char *endptr;
    float val = strtof(str, &endptr);
    if (endptr == str) return false;
    if (var != NULL) *var = val;
    return true;
}

size_t parse_all(const char *str, int *var, size_t size) {
    size_t len = str ? strlen(str) : 0, idx = 0;
    const char *headptr = str;
    char *endptr;
    int val = 0;
    while ((size_t)(headptr - str) < len && idx < size) {
        val = strtol(headptr, &endptr, 0);
        if (headptr != endptr) var[idx++] = val;
        headptr = endptr + 1;
    }
    return idx;
}

char * cast_away_const(const char *str) {
    return (char *)(void *)(const void *)str;
}

static char hexdigits(uint8_t v) {
    if (v < 10) return v + '0';
    if (v > 15) return 'Z';
    return v - 10 + 'A';
}

void hexdump(const void *src, size_t bytes, size_t maxlen) {
    size_t maxbytes = maxlen / 3, count = MIN(bytes, maxbytes);
    LOOPN(i, count) printf(" %02X", ((uint8_t *)src)[i++]);
    if (bytes && maxbytes && bytes > maxbytes)
        printf(" ... [%u/%u]", count, bytes);
    putchar('\n');
}

char * hexdumps(const void *src, char *dst, size_t bytes, size_t maxlen) {
    size_t maxbytes = maxlen / 2, count = MIN(bytes, maxbytes);
    LOOPN(i, count) {
        dst[2 * i + 0] = hexdigits(((uint8_t *)src)[i] >> 4);
        dst[2 * i + 1] = hexdigits(((uint8_t *)src)[i] & 0xF);
    }
    if (bytes && maxbytes && bytes > maxbytes) {
        sprintf(dst + count * 2, " ... [%u/%u]", count, bytes);
    } else {
        dst[count * 2] = '\0';
    }
    return dst;
}

const char * format_sha256(const void *src, size_t len) {
    static char buf[64 + 1];
    if (!src || !len) { buf[0] = '\0'; return buf; }
    return hexdumps(src, buf, len, 64);
}

// Note: format_size format string into static buffer. Therefore you don't
// need to free the buffer after logging/strcpy etc. But you must save the
// result before calling format_size once again, because the buffer will be
// reused and overwriten.
const char * format_size(uint64_t bytes, bool inbit) {
    static char buffer[16]; // xxxx.xx u\0
    static char * units[] = { " ", "K", "M", "G", "T", "P" };
    static int Bdems[] = { 0, 1, 2, 3, 3, 4 };
    static int bdems[] = { 0, 2, 3, 3, 4, 7 };
    if (!bytes) return inbit ? "0 b" : "0 B";
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

static bool task_compare(uint8_t sort_attr, TaskStatus_t *a, TaskStatus_t *b) {
    int aid = a->xCoreID > 1 ? -1 : a->xCoreID;
    int bid = b->xCoreID > 1 ? -1 : b->xCoreID;
    if (sort_attr == 0) return a->xTaskNumber < b->xTaskNumber;
    if (sort_attr == 1) return a->eCurrentState < b->eCurrentState;
    if (sort_attr == 2) {
        int rst = strcmp(a->pcTaskName, b->pcTaskName);
        return rst ? rst < 0 : aid < bid;
    }
    if (sort_attr == 3) return a->uxCurrentPriority < b->uxCurrentPriority;
    if (sort_attr == 4) return aid < bid;
    if (sort_attr == 5) return a->ulRunTimeCounter < b->ulRunTimeCounter;
    return a->usStackHighWaterMark < b->usStackHighWaterMark;
}

void task_info(uint8_t sort_attr) {
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
        LOOP(j, i + 1, num) {
            if (task_compare(sort_attr, tasks + i, tasks + j)) continue;
            tmp = tasks[i];
            tasks[i] = tasks[j];
            tasks[j] = tmp;
        }
    }
#   ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    LOOPN(i, num) { ulTotalRunTime += tasks[i].ulRunTimeCounter; }
#   endif
    printf("TID State Name            Pri CPU Used StackHW\n");
    LOOPN(i, num) {
        if (!strcmp(taskname = tasks[i].pcTaskName, "IDLE"))
            taskname = tasks[i].xCoreID ? "CPU 1 App" : "CPU 0 Pro";
        printf("%3d  (%c)  %-15s %2d  %3d %3d%% %7s\n",
               tasks[i].xTaskNumber, task_states[tasks[i].eCurrentState],
               taskname, tasks[i].uxCurrentPriority,
               tasks[i].xCoreID > 1 ? -1 : tasks[i].xCoreID,
               100 * tasks[i].ulRunTimeCounter / ulTotalRunTime,
               format_size(tasks[i].usStackHighWaterMark, false));
    }
    vPortFree(tasks);
#else
    puts("Unsupported command! Enable `CONFIG_FREERTOS_USE_TRACE_FACILITY` "
         "in menuconfig/sdkconfig to run this command");
    NOTUSED(sort_attr);
#endif
}

void version_info() {
    const esp_app_desc_t *desc = esp_ota_get_app_description();
    printf("ESP-IDF  %s\n"
           "FreeRTOS %s\n"
           "Firmware %s%s%s\n"
           "Compiled %s %s\n",
           esp_get_idf_version(), tskKERNEL_VERSION_NUMBER,
           Config.info.VER, strlen(Config.info.VER) ? " " : "",
           desc->version, __DATE__, __TIME__);
}

static uint32_t memory_types[] = {
    0, MALLOC_CAP_SPIRAM, MALLOC_CAP_EXEC, MALLOC_CAP_DMA,
    MALLOC_CAP_INTERNAL, MALLOC_CAP_DEFAULT
};

static const char * const memory_names[] = {
    "TOTAL", "SPI RAM", "EXEC", "DMA", "INTERN", "DEFAULT"
};

void memory_info() {
    uint32_t mem_type = 0;
    multi_heap_info_t info;
    printf("Type       Avail     Used     Size Free\n");
    LOOPND(i, LEN(memory_types)) {
        heap_caps_get_info(&info, i ? memory_types[i] : mem_type);
        size_t total = info.total_free_bytes + info.total_allocated_bytes;
        if (total) mem_type |= memory_types[i];
        printf("%-7s", memory_names[i]);
        printf(" %8s", format_size(info.total_allocated_bytes, false));
        printf(" %8s", format_size(info.total_free_bytes, false));
        printf(" %8s", format_size(total, false));
        printf(" %3d%%", total ? 100 * info.total_free_bytes / total : 0);
        putchar('\n');
    }
}

static const char * chip_model_str(esp_chip_model_t model) {
    switch (model) {
        CASESTR(CHIP_ESP32S2, 5);
        CASESTR(CHIP_ESP32S3, 5);
        CASESTR(CHIP_ESP32C3, 5);
        CASESTR(CHIP_ESP32H2, 5);
        case CHIP_ESP32:
#ifndef CONFIG_IDF_TARGET_ESP32
            return "ESP32";
#else
            break;
#endif
        default: return "Unknown";
    }
#ifdef CONFIG_IDF_TARGET_ESP32
    switch (REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_VER_PKG)) {
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6: return "ESP32-D0WD-Q6";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5: return "ESP32-D0WD-Q5";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5: return "ESP32-D2WD-Q5";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4: return "ESP32-PICO-D4";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302: return "ESP32-PICO-V3-02";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3: return "ESP32-D2WD-R2-V3";
        default: return "Unknown";
    }
#endif
}

void hardware_info() {
    esp_chip_info_t info;
    esp_chip_info(&info);
    printf(
        "Chip UID: %s-%s\n"
        "   Model: %s\n"
        "   Cores: %d\n"
        "Revision: %d\n"
        "Features: %s %s flash%s%s%s%s\n",
        Config.info.NAME, Config.info.UID,
        chip_model_str(info.model), info.cores, info.revision,
        format_size(spi_flash_get_chip_size(), false),
        info.features & CHIP_FEATURE_EMB_FLASH ? "Embedded" : "External",
        info.features & CHIP_FEATURE_EMB_PSRAM ? " | Embedded PSRAM" : "",
        info.features & CHIP_FEATURE_WIFI_BGN ? " | WiFi 802.11bgn" : "",
        info.features & CHIP_FEATURE_BLE ? " | BLE" : "",
        info.features & CHIP_FEATURE_BT ? " | BT" : ""
    );

    uint8_t sta[6], ap[6];
    esp_read_mac(sta, ESP_MAC_WIFI_STA);
    esp_read_mac(ap, ESP_MAC_WIFI_SOFTAP);
    printf(" STA MAC: " MACSTR "\n", MAC2STR(sta));
    printf(" AP  MAC: " MACSTR "\n", MAC2STR(ap));
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

static uint8_t partition_used(const esp_partition_t *part) {
    if (part->type == ESP_PARTITION_TYPE_APP) {
        esp_image_metadata_t data = { .start_addr = part->address };
        const esp_partition_pos_t pos = {
            .offset = part->address,
            .size = part->size
        };
        const char *tag = "esp_image";
        esp_log_level_t backup = esp_log_level_get(tag);
        esp_log_level_set(tag, ESP_LOG_NONE);
        esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &data);
        esp_log_level_set(tag, backup);
        if (!err) return 100 * data.image_len / part->size;
    } else if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS) {
        nvs_stats_t nvs_stats;
        if (!nvs_get_stats(part->label, &nvs_stats))
            return 100 * nvs_stats.used_entries / nvs_stats.total_entries;
    } else if (
        part->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT ||
        part->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS
    ) {
        filesys_info_t info;
        filesys_ffs_info(&info);
        if (info.total) return 100 * info.used / info.total;
    }
    return 0;
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
    printf("LabelName    Type SubType  Offset   Size     Used Secure\n");
    while (num--) {
        part = parts[num];
        printf("%-12s %-4s %-8s 0x%06X 0x%06X %3d%% %s\n",
               part->label, partition_type_str(part->type),
               partition_subtype_str(part->type, part->subtype),
               part->address, part->size, partition_used(part),
               part->encrypted ? "true" : "false");
    }
}
