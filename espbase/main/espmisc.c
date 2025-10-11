/* 
 * File: espmisc.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-20 21:37:14
 */

#include "globals.h"
#include "filesys.h"            // for filesys_get_info
#include "drivers.h"            // for gpio_usage
#include "config.h"

#include "nvs.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "bootloader_common.h"
#include "soc/efuse_periph.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t errval(int error) {
    switch (error) {
    case 0:         return ESP_OK;
    case ENOMEM:    return ESP_ERR_NO_MEM;
    case EINVAL:    return ESP_ERR_INVALID_ARG;
    case EBUSY:     return ESP_ERR_INVALID_STATE;
    case EEXIST:    return ESP_ERR_INVALID_STATE;
    case ERANGE:    return ESP_ERR_INVALID_SIZE;
    case ENOSPC:    return ESP_ERR_INVALID_SIZE;
    case ENOENT:    return ESP_ERR_NOT_FOUND;
    case ENOTSUP:   return ESP_ERR_NOT_SUPPORTED;
    case ETIMEDOUT: return ESP_ERR_TIMEOUT;
#ifndef IDF_TARGET_V4
    case EPERM:     return ESP_ERR_NOT_ALLOWED;
#endif
    default:        return ESP_FAIL;
    }
}

const char *errstr(int error) {
    esp_err_t err = errval(error);
    return err == ESP_FAIL && error ? strerror(error) : esp_err_to_name(err);
}

void reboot(void *arg) {
    if (!arg) esp_restart();
    const char *T = Config.info.NAME;
    reboot_ctx_t *ptr = (reboot_ctx_t *)arg;
    reboot_ctx_t ctx = *ptr;                            // local copy
    if (ctx.timeout_ms) {
        uint32_t ts = pdTICKS_TO_MS(xTaskGetTickCount());
        if (ctx.target_ms > ts) {
            ESP_LOGW(T, "Will reboot in %s", format_time(ctx.target_ms - ts));
            return;
        }
        if (xTaskGetCurrentTaskHandle() != ctx.task) {  // create or update
            TRYNULL(ctx.task, vTaskDelete);
            if (xTaskCreate(
                reboot, "reboot", 4096, &ctx, 20, (TaskHandle_t *)&ctx.task
            ) == pdPASS) {
                ESP_LOGW(T, "Will reboot in %s", format_time(ctx.timeout_ms));
                ptr->target_ms = ctx.target_ms = ts + ctx.timeout_ms;
                ptr->task = ctx.task;
                return;
            }                                           // ERR_NO_MEM
        } else {
            msleep(ctx.timeout_ms);                     // in task
        }
    }
    if (ctx.halt) {
#ifdef CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT
        esp_system_abort("Manually shutdown");
#else
        ESP_LOGW(T, "CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT not set! Will reboot");
#endif
    }
    esp_restart();
}

void msleep(uint32_t ms) { vTaskDelay(TIMEOUT(ms)); }

uint64_t asleep(uint32_t ms, uint64_t next) {
    uint64_t curr = xTaskGetTickCount();
    if (curr < next) {
        vTaskDelay(next - curr);
    } else if (!next || (curr - next) > ms) {
        next = curr;
    }
    return next + TIMEOUT(ms);
}

size_t parse_pin(const char *str, int *arr, size_t len, const char **names) {
    size_t num = parse_all(str, arr, len);
    if (!names || !num) return 0;
    LOOPN(i, num) {
        if (arr[i] == GPIO_NUM_NC || !names[i]) continue;
        const char *usage = gpio_usage(arr[i], names[i]);
        if (usage != names[i]) return 0;
    }
    return num;
}

size_t gbk2str_r(const char *src, char *dst, size_t dlen) {
    FILE *fd = fopen(fjoin(2, Config.sys.DIR_DATA, "gbktable.bin"), "r");
    if (!fd && filesys_get_info(FILESYS_SDCARD, NULL))
        fd = fopen(snorm("gbktable.bin"), "r");
    if (!fd) return 0;
    uint32_t unicode, used, slen = strlen(src), sidx = 0, didx = 0;
    while (sidx < slen && didx < dlen) {
        if (!( used = gbk2unicode(fd, src + sidx, &unicode) )) break;
        didx += snprintf(dst + didx, dlen - didx, unicode2str(unicode));
        sidx += used;
    }
    TRYNULL(fd, fclose);
    return sidx;
}

char * gbk2str(const char *src) {
    char *dst = NULL;
    size_t slen = strlen(src), dlen = slen / 2 * 3 + 1;
    if (EMALLOC(dst, dlen) || gbk2str_r(src, dst, dlen) != slen) TRYFREE(dst);
    return dst;
}

static uint8_t utpls[][10] = {
    { 0x25, 0x05, 0xCB, 0xD4, 0xD1, 0xD5, 0xCF },                   // ○◔◑◕●
    { 0x25, 0x18, 0x8F, 0x8E, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x88 }, // ▏▎▍▌▋▊▉█
    { 0x25, 0x18, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88 }, // ▁▂▃▄▅▆▇█
    { 0x25, 0x14, 0x91, 0x92, 0x93, 0x88 },                         // ░▒▓█
    { 0x28, 0x08, 0x46, 0x07, 0x0B, 0x19, 0x38, 0xB0, 0xE0, 0xC4 }, // ⡆⠇⠋⠙⠸⢰⣠⣄
};

esp_err_t unicode_tricks(const unicode_trick_t *cfg) {
    if (cfg->index >= LEN(utpls)) return ESP_ERR_INVALID_ARG;
    uint8_t *tpl = utpls[cfg->index], loop = tpl[1] & 0x10;
    uint8_t iter = tpl[1] & 0xF, count = MAX(1, cfg->repeat);
    uint16_t base = tpl[0] << 8, intv = 1000 / iter;
    FILE *stream = cfg->stream ?: stderr;
    LOOPN(i, cfg->timeout_ms / intv) {
        uint8_t idx = i % iter;
        if (loop && (i / iter) % 2) idx = iter - 1 - idx;
        const char *str = unicode2str(base | tpl[2 + idx]);
        fputc('\r', stream);
        LOOPN(j, count) { fprintf(stream, str); }
        fflush(stream);
        msleep(intv);
    }
    fputc('\n', stream);
    fflush(stream);
    return ESP_OK;
}

const char * format_size(double bytes) {
    static const int dems[] = { 0, 1, 2, 3, 3, 4 };
    static char buf[16]; // xxxx.xx u\0
    int exp = 0;
    while (exp < LEN(dems) && bytes > 1024) {
        bytes /= 1024;
        exp++;
    }
    snprintf(buf, sizeof(buf), "%.*f %cB", dems[exp], bytes, " KMGTP"[exp]);
    return buf;
}

const char * format_time(double ms) {
    static char buf[16];
    if (ms < 1e3) {
        snprintf(buf, sizeof(buf), "%3.0f ms", ms);
    } else if (ms < 60e3) {
        snprintf(buf, sizeof(buf), "%4.1f s", ms / 1e3);
    } else if (ms < 3600e3) {
        snprintf(buf, sizeof(buf), "%4.1f m", ms / 60e3);
    } else if (ms < 86400e3) {
        snprintf(buf, sizeof(buf), "%4.1f h", ms / 3600e3);
    } else {
        snprintf(buf, sizeof(buf), "%4.1f d", ms / 86400e3);
    }
    return buf;
}

const char * format_sha256(const void *src, size_t len) {
    static char buf[64 + 1];
    if (!src || !len) { buf[0] = '\0'; return buf; }
    return hexdumps(src, buf, len, sizeof(buf));
}

const char * format_binary(uint64_t val, size_t maxbits) {
    static char buf[64 + 1];
    size_t bits = MIN(maxbits, sizeof(val) * 8);
    LOOPN(i, bits) {
        buf[bits - i - 1] = (val & (1 << i)) ? '1' : '0';
    }
    buf[bits] = '\0';
    return buf;
}

bool notify_increase(void *task) {
    if (!task) return false;
    if (!xPortInIsrContext()) return xTaskNotifyGive(task);
    vTaskNotifyGiveFromISR(task, NULL);
    return true;
}

bool notify_decrease(void *task) {
    uint32_t val = 0;
    if (xPortInIsrContext()) {
        if (task) xTaskNotifyAndQueryFromISR(task, 0, eNoAction, &val, NULL);
        if (!val) return false;
        return xTaskNotifyFromISR(task, val - 1, eSetValueWithOverwrite, NULL);
    } else {
        if (task) xTaskNotifyAndQuery(task, 0, eNoAction, &val);
        if (!val) return false;
        return xTaskNotify(task, val - 1, eSetValueWithOverwrite);
    }
}

bool notify_wait_for(uint32_t target, uint32_t tout_ms, uint32_t wait_ms) {
    uint32_t val = ulTaskNotifyValueClear(NULL, 0); // get value without clear
    TickType_t wait = TIMEOUT(wait_ms), tout = TIMEOUT(tout_ms);
    if (val == target && wait && !xTaskNotifyWait(0, 0, &val, wait))
        return true; // target matched and not notified in wait_ms
    TickType_t ts = xTaskGetTickCount(), dt;
    while (val != target && tout) {
        if (!xTaskNotifyWait(0, 0, &val, tout)) break;
        if (tout != portMAX_DELAY) {
            if (( dt = xTaskGetTickCount() - ts ) > tout) break;
            tout -= dt;
            ts += dt;
        }
    }
    return val == target;
}

#define TASK_MAXID  29
#define TASK_TIME   30
#define TASK_TOTAL  31
static uint32_t task_hist[TASK_TOTAL + 1];

static bool task_compare(tsort_t sort, TaskStatus_t *a, TaskStatus_t *b) {
    int aid = a->xCoreID > 1 ? -1 : a->xCoreID;
    int bid = b->xCoreID > 1 ? -1 : b->xCoreID;
    int rst = strcmp(a->pcTaskName, b->pcTaskName);
    switch (sort) {
    case TSORT_STATE: return a->eCurrentState < b->eCurrentState;
    case TSORT_TID:   return a->xTaskNumber < b->xTaskNumber;
    case TSORT_CPU:   return aid < bid;
    case TSORT_PRI:   return a->uxCurrentPriority < b->uxCurrentPriority;
    case TSORT_NAME:  return rst ? rst < 0 : aid < bid;
    case TSORT_USAGE:
        if (a->xTaskNumber <= TASK_MAXID && b->xTaskNumber <= TASK_MAXID)
            return (a->ulRunTimeCounter - task_hist[a->xTaskNumber]) <
                   (b->ulRunTimeCounter - task_hist[b->xTaskNumber]);
        return a->ulRunTimeCounter < b->ulRunTimeCounter;
    default:          return a->usStackHighWaterMark < b->usStackHighWaterMark;
    }
}

void task_info(tsort_t sort) {
#ifdef CONFIG_FREERTOS_USE_TRACE_FACILITY
    uint32_t total = 0, curr = xTaskGetTickCount(), dt;
    uint16_t num = uxTaskGetNumberOfTasks();
    TaskStatus_t tmp, *tasks = pvPortMalloc(num * sizeof(TaskStatus_t));
    if (tasks == NULL) {
        printf("Could not allocate space for tasks list");
        return;
    }
    if (!( num = uxTaskGetSystemState(tasks, num, &total) )) {
        printf("TaskStatus_t array size too small. Skip");
        goto exit;
    }
#   ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    LOOPN(i, num) { total += tasks[i].ulRunTimeCounter; }
#   endif
    LOOPN(i, num) { LOOP(j, i + 1, num) {       // sort tasks by sort_attr
        if (task_compare(sort, tasks + i, tasks + j)) continue;
        tmp = tasks[i]; tasks[i] = tasks[j]; tasks[j] = tmp;
    } }
    printf("S ID CPU Pri Name            StackHW Used %s\n",
            format_time(pdTICKS_TO_MS(curr - task_hist[TASK_TIME])));
    LOOPN(i, num) {
        printf(
            "%c %-2d %3d %3d %-15s %7s %3.0f%% ",
            // Current Ready Blocked Suspended Deleted
            "*RBSD"[tasks[i].eCurrentState], tasks[i].xTaskNumber,
            tasks[i].xCoreID > 1 ? -1 : tasks[i].xCoreID,
            tasks[i].uxCurrentPriority, tasks[i].pcTaskName,
            format_size(tasks[i].usStackHighWaterMark),
            1e2 * tasks[i].ulRunTimeCounter / total
        );
        if (tasks[i].xTaskNumber > TASK_MAXID) {
            putchar('\n');
        } else {
            dt = tasks[i].ulRunTimeCounter - task_hist[tasks[i].xTaskNumber];
            task_hist[tasks[i].xTaskNumber] = tasks[i].ulRunTimeCounter;
            printf("%5.0f%%\n", 1e2 * dt / (total - task_hist[TASK_TOTAL]));
        }
    }
    task_hist[TASK_TIME] = curr;
    task_hist[TASK_TOTAL] = total;
exit:
    vPortFree(tasks);
#else
    puts("Unsupported command! Enable `CONFIG_FREERTOS_USE_TRACE_FACILITY` "
         "in menuconfig/sdkconfig to run this command");
    NOTUSED(sort_attr);
#endif
}

void version_info() {
    printf(
        "ESP  IDF: %s\n"
        "FreeRTOS: %s\n"
        "Firmware: %s\n"
        "Compiled: %s %s\n",
        esp_get_idf_version(), tskKERNEL_VERSION_NUMBER,
        Config.info.VER, __DATE__, __TIME__
    );
}

void memory_info() {
    const uint32_t caps[] = {
        MALLOC_CAP_DEFAULT, MALLOC_CAP_INTERNAL, MALLOC_CAP_SPIRAM,
        MALLOC_CAP_DMA, MALLOC_CAP_EXEC
    };
    const char * const names[] = {
        "DEFAULT", "INTERN", "SPI RAM", "DMA", "EXEC"
    };
    multi_heap_info_t info;
    printf("%-7s %8s %8s %4s %4s %s\n",
           "Type", "Total", "Avail", "Used", "Frag", "Caps");
    LOOPN(i, LEN(caps)) {
        heap_caps_get_info(&info, caps[i]);
        size_t tfree = info.total_free_bytes;
        size_t tfrag = tfree - info.largest_free_block;
        size_t total = tfree + info.total_allocated_bytes;
        printf("%-7s %8s ", names[i], format_size(total));
        printf("%8s ", format_size(tfree));
        printf("%3d%% ", total ? 100 * info.total_allocated_bytes / total : 0);
        printf("%3d%% ", tfree ? 100 * tfrag / tfree : 0);
        printf("0x%08" PRIu32 "\n", caps[i]);
    }
}

static const char * chip_model_str(esp_chip_model_t model) {
    switch (model) {
    case CHIP_ESP32S2:  return "ESP32-S2";
    case CHIP_ESP32S3:  return "ESP32-S3";
    case CHIP_ESP32C3:  return "ESP32-C3";
    case CHIP_ESP32H2:  return "ESP32-H2";
#ifndef CONFIG_IDF_TARGET_ESP32
    case CHIP_ESP32:    return "ESP32";
#else
    case CHIP_ESP32:
        switch (bootloader_common_get_chip_ver_pkg()) {
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ6:     return "ESP32-D0WD-Q6";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDQ5:     return "ESP32-D0WD-Q5";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D2WDQ5:     return "ESP32-D2WD-Q5";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4:     return "ESP32-PICO-D4";
        case EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302:   return "ESP32-PICO-V3-02";
        case EFUSE_RD_CHIP_VER_PKG_ESP32D0WDR2V3:   return "ESP32-D2WD-R2-V3";
        }
        FALLTH;
#endif
    default:            return "Unknown";
    }
}

void hardware_info() {
    uint32_t fid, size;
    esp_chip_info_t info;
    esp_chip_info(&info);
    esp_flash_read_id(NULL, &fid);
    esp_flash_get_physical_size(NULL, &size);
#ifdef IDF_TARGET_V4
    uint16_t revision = info.full_revision;
#else
    uint16_t revision = info.revision;
#endif
    printf(
        "Chip UID: %s-%s\n"
        "   Model: %s\n"
        "   Cores: %d\n"
        "Revision: %d.%d\n"
        "Features: %s %s flash%s%s%s%s\n"
        "   Flash: MID %02X CID %04X\n",
        Config.info.NAME, Config.info.UID,
        chip_model_str(info.model), info.cores,
        revision / 100, revision % 100, format_size(size),
        info.features & CHIP_FEATURE_EMB_FLASH ? "Embedded" : "External",
        info.features & CHIP_FEATURE_EMB_PSRAM ? " | Embedded PSRAM" : "",
        info.features & CHIP_FEATURE_WIFI_BGN ? " | WiFi 802.11bgn" : "",
        info.features & CHIP_FEATURE_BLE ? " | BLE" : "",
        info.features & CHIP_FEATURE_BT ? " | BT" : "",
        (uint8_t)(fid >> 16), (uint16_t)(fid & 0xFFFF)
    );

    const struct {
        const char * name;
        esp_mac_type_t type;
    } macs[] = {
        { "STA", ESP_MAC_WIFI_STA },
        { "AP ", ESP_MAC_WIFI_SOFTAP },
        { "BT ", ESP_MAC_BT },
        { "ETH", ESP_MAC_ETH },
    };
    LOOPN(i, LEN(macs)) {
        uint8_t buf[6] = { 0 };
        if (esp_read_mac(buf, macs[i].type)) continue;
        printf(" %s MAC: " MACSTR "\n", macs[i].name, MAC2STR(buf));
    }
}

static const char * partition_subtype_str(
    esp_partition_type_t type, esp_partition_subtype_t subtype
) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", subtype);
    if (type == ESP_PARTITION_TYPE_DATA) {
        switch (subtype) {
        case ESP_PARTITION_SUBTYPE_DATA_OTA:        return "OTA";
        case ESP_PARTITION_SUBTYPE_DATA_PHY:        return "PHY";
        case ESP_PARTITION_SUBTYPE_DATA_NVS:        return "NVS";
        case ESP_PARTITION_SUBTYPE_DATA_COREDUMP:   return "COREDUMP";
        case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS:   return "NVS_KEYS";
        case ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM:   return "EFUSE_EM";
        case ESP_PARTITION_SUBTYPE_DATA_UNDEFINED:  return "UNDEFINED";
        case ESP_PARTITION_SUBTYPE_DATA_ESPHTTPD:   return "ESPHTTPD";
        case ESP_PARTITION_SUBTYPE_DATA_FAT:        return "FAT";
        case ESP_PARTITION_SUBTYPE_DATA_SPIFFS:     return "SPIFFS";
        default: break;
        }
    } else if (type == ESP_PARTITION_TYPE_APP) {
        switch (subtype) {
        case ESP_PARTITION_SUBTYPE_APP_FACTORY:     return "FACTORY";
        case ESP_PARTITION_SUBTYPE_APP_TEST:        return "TEST";
        default:
            if (
                subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
                subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX
            ) {
                subtype -= ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
                snprintf(buf, sizeof(buf), "OTA_%d", subtype);
            }
            break;
        }
    }
    return buf;
}

static const char * partition_type_str(esp_partition_type_t type) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", type);
    switch (type) {
    case ESP_PARTITION_TYPE_DATA:                   return "DATA";
    case ESP_PARTITION_TYPE_APP:                    return "APP";
    default:                                        return buf;
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
        if (!err) return part->size ? 100 * data.image_len / part->size : 0;
    } else if (part->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS) {
        nvs_stats_t stat;
        if (!nvs_get_stats(part->label, &stat) && stat.total_entries)
            return 100 * stat.used_entries / stat.total_entries;
#ifdef CONFIG_BASE_USE_FFS
    } else if (
        part->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT ||
        part->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS ||
        !strcmp(part->label, CONFIG_BASE_FFS_PART)
    ) {
        filesys_info_t info;
        if (filesys_get_info(FILESYS_FLASH, &info) && info.total)
            return 100 * info.used / info.total;
#endif
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
    printf("Label        Type SubType   Offset   Size     Used Secure\n");
    while (num--) {
        part = parts[num];
        esp_partition_subtype_t subtype = part->subtype;
#ifdef CONFIG_BASE_USE_FFS
        if (!strcmp(part->label, CONFIG_BASE_FFS_PART)) {
#   ifdef CONFIG_BASE_FFS_FAT
            subtype = ESP_PARTITION_SUBTYPE_DATA_FAT;
#   else
            subtype = ESP_PARTITION_SUBTYPE_DATA_SPIFFS;
#   endif
        }
#endif
        printf("%-12s %-4s %-9s 0x%06" PRIX32 " 0x%06" PRIX32 " %3d%% %s\n",
               part->label, partition_type_str(part->type),
               partition_subtype_str(part->type, subtype),
               part->address, part->size, partition_used(part),
               part->encrypted ? "true" : "false");
    }
}
