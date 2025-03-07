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
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_image_format.h"
#include "soc/efuse_periph.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void msleep(uint32_t ms) { vTaskDelay(ms / portTICK_PERIOD_MS); }

uint64_t asleep(uint32_t ms, uint64_t next) {
    uint64_t curr = xTaskGetTickCount();
    if (!next) {
        next = curr;
    } else if (curr < next) {
        vTaskDelay(next - curr);
    }
    next += ms * portTICK_PERIOD_MS;
    return next;
}

bool strbool(const char *str) {
    if (!str) return false;
    return !strcmp(str, "1") || !strcasecmp(str, "y") || !strcasecmp(str, "on");
}

char * strtrim(char *str, const char *chars) {
    size_t slen = strlen(str ?: "");
    if (!slen || !chars) return str;
    size_t head = strspn(str, chars);
    size_t tail = slen - 1;
    while (tail > head && strchr(chars, str[tail])) { tail--; }
    str[++tail] = '\0';
    return str + head;
}

char * b64encode(char *buf, const char *inp, size_t len) {
    const char *end = inp + len, chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!inp || (!buf && ECALLOC(buf, 1, (len + 2) / 3 * 4 + 1))) return buf;
    for (char *out = buf; inp < end; inp += 3, out += 4) {
        uint32_t u24 = inp[0] << 16;
        if ((inp + 1) < end) u24 |= inp[1] << 8;
        if ((inp + 2) < end) u24 |= inp[2];
        out[0] = chars[(u24 >> 18) & 0x3F];
        out[1] = chars[(u24 >> 12) & 0x3F];
        out[2] = (inp + 1) >= end ? '=' : chars[(u24 >> 6) & 0x3F];
        out[3] = (inp + 2) >= end ? '=' : chars[u24 & 0x3F];
    }
    return buf;
}

bool endswith(const char *str, const char *tail) {
    if (!str || !tail) return false;
    int offset = strlen(str) - strlen(tail);
    return !strcmp(str + MAX(offset, 0), tail);
}

bool startswith(const char *str, const char *head) {
    if (!str || !head) return false;
    size_t headlen = strlen(head);
    return strlen(str) < headlen ? false : !strncmp(str, head, headlen);
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

static uint8_t numdigits(int n) {
    uint8_t i = !n;
    while (n) { n /= 10; i++; }
    return i;
}

static char hexdigits(uint8_t v) {
    if (v < 10) return v + '0';
    if (v > 15) return hexdigits(v % 16);
    return v - 10 + 'A';
}

void hexdump(const void *src, size_t bytes, size_t maxlen) {
    size_t maxbytes = maxlen / 3, count = MIN(bytes, maxbytes);
    LOOPN(i, count) { printf("%02X ", ((uint8_t *)src)[i]); }
    if (bytes && maxbytes && bytes > maxbytes)
        printf("... [%u/%u]", count, bytes);
    putchar('\n');
}

char * hexdumps(const void *src, char *dst, size_t bytes, size_t maxlen) {
    if (!dst || !maxlen) return 0;
    size_t maxbytes, count, offset = 0;
    if ((bytes * 2 + 1) <= maxlen) {
        maxbytes = (maxlen - 1) / 2;
    } else if (maxlen < (9 + numdigits(bytes) + numdigits(maxlen / 2))) {
        dst[0] = '\0';
        return dst; // no space
    } else { // append tail to dst
        offset = maxlen - 9 - numdigits(bytes);
        maxbytes = (offset - numdigits(maxlen / 2)) / 2;
    }
    count = MIN(bytes, maxbytes);
    LOOPN(i, count) {
        dst[2 * i + 0] = hexdigits(((uint8_t *)src)[i] >> 4);
        dst[2 * i + 1] = hexdigits(((uint8_t *)src)[i] & 0xF);
    }
    if (offset) {
        sprintf(dst + offset - numdigits(count), " ... [%u/%u]", count, bytes);
    } else {
        dst[count * 2] = '\0';
    }
    return dst;
}

// Convert between Unicode & Bytes
// 0x00000000 - 0x0000007F : 0b0xxxxxxx
// 0x00000080 - 0x000007FF : 0b110xxxxx 0b10xxxxxx
// 0x00000800 - 0x0000FFFF : 0b1110xxxx 0b10xxxxxx 0b10xxxxxx
// 0x00010000 - 0x001FFFFF : 0b11110xxx 0b10xxxxxx 0b10xxxxxx 0b10xxxxxx
const char * unicode2str(uint32_t unicode) {
    static char buf[5];
    if (unicode < 0x80) {
        buf[0] = unicode;
        buf[1] = '\0';
    } else if (unicode < 0x800) {
        buf[0] = 0xC0 | ((unicode >> 6) & 0x1F);
        buf[1] = 0x80 | ((unicode >> 0) & 0x3F);
        buf[2] = '\0';
    } else if (unicode < 0x10000) {
        buf[0] = 0xE0 | ((unicode >> 12) & 0xF);
        buf[1] = 0x80 | ((unicode >> 6) & 0x3F);
        buf[2] = 0x80 | ((unicode >> 0) & 0x3F);
        buf[3] = '\0';
    } else if (unicode < 0x1FFFFF) {
        buf[0] = 0xF0 | ((unicode >> 18) & 0x7);
        buf[1] = 0x80 | ((unicode >> 12) & 0x3F);
        buf[2] = 0x80 | ((unicode >> 6) & 0x3F);
        buf[3] = 0x80 | ((unicode >> 0) & 0x3F);
        buf[4] = '\0';
    } else {
        buf[0] = '\0';
    }
    return buf;
}

uint32_t str2unicode(const char *str) {
    if (!str || str[0] > 0xF7) return 0;
    uint32_t nbytes = 0, unicode = 0;
    if (str[0] >= 0xF0) {
        nbytes = 3;
        unicode = str[0] & 0x7;
    } else if (str[0] >= 0xE0) {
        nbytes = 2;
        unicode = str[0] & 0xF;
    } else if (str[0] >= 0xC0) {
        nbytes = 1;
        unicode = str[0] & 0x1F;
    } else {
        return str[0];
    }
    for (uint32_t i = 1; i <= nbytes; i++) {
        if ((str[i] >> 6) != 0x2) return 0; // invalid format
        unicode = (unicode << 6) | (str[i] & 0x3F);
    }
    return unicode;
}

static const uint8_t unicode_table[][10] = {
    { 0x25, 6, 0xCB, 0xD4, 0xD1, 0xD5, 0xCF },                      // circle
    { 0x25, 8, 0x8F, 0x8E, 0x8D, 0x8C, 0x8B, 0x8A, 0x89, 0x88 },    // v bars
    { 0x25, 8, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88 },    // h bars
    { 0x25, 4, 0x91, 0x92, 0x93, 0x89 },                            // shades
    { 0x28, 8, 0x46, 0x07, 0x0B, 0x19, 0x38, 0xB0, 0xE0, 0xC4 },    // dots
};

esp_err_t unicode_tricks(const unicode_trick_t *conf) {
    if (conf->index >= LEN(unicode_table)) return ESP_ERR_INVALID_ARG;
    uint16_t base = (uint16_t)unicode_table[conf->index][0] << 8;
    uint16_t intv = conf->timeout_ms / unicode_table[conf->index][1];
    uint8_t count = MAX(conf->repeat, 1);
    FILE *stream = conf->stream ?: stderr;
    LOOPN(i, unicode_table[conf->index][1]) {
        uint8_t code = unicode_table[conf->index][i + 2];
        fputc('\r', stream);
        LOOPN(j, count) { fprintf(stream, unicode2str(base | code)); }
        fflush(stream);
        if (intv) msleep(intv);
    }
    fputc('\n', stream);
    fflush(stream);
    return ESP_OK;
}

const char * format_sha256(const void *src, size_t len) {
    static char buf[64 + 1];
    if (!src || !len) { buf[0] = '\0'; return buf; }
    return hexdumps(src, buf, len, sizeof(buf));
}

const char * format_binary(uint64_t num, size_t bytes) {
    static char buf[64 + 1];
    size_t bits = MIN(bytes, sizeof(num)) * 8;
    LOOPN(i, bits) {
        buf[bits - i - 1] = (num & (1 << i)) ? '1' : '0';
    }
    buf[bits] = '\0';
    return buf;
}

const char * format_size(uint64_t bytes, bool inbit) {
    static char buffer[16]; // xxxx.xx u\0
    static const char * units[] = { " ", "K", "M", "G", "T", "P" };
    static const int Bdems[] = { 0, 1, 2, 3, 3, 4 };
    static const int bdems[] = { 0, 2, 3, 3, 4, 7 };
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

static void * createTimer(int64_t us, void (*func)(void *), void *arg) {
    esp_timer_handle_t hdl = NULL;
    const esp_timer_create_args_t args = { .callback = func, .arg = arg };
    if (!func || esp_timer_create(&args, &hdl) != ESP_OK) {
        hdl = NULL;
    } else if (us < 0) {
        esp_timer_start_once(hdl, -us);
    } else {
        esp_timer_start_periodic(hdl, us);
    }
    return hdl;
}

void * setTimeout(uint32_t ms, void (*func)(void *), void *arg) {
    return createTimer((int64_t)ms * -1000, func, arg);
}

void * setInterval(uint32_t ms, void (*func)(void *), void *arg) {
    return createTimer((int64_t)ms * +1000, func, arg);
}

void clearTimer(void *hdl) { TRYNULL(hdl, esp_timer_delete); }

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
    //                              Running Ready Blocked Suspended Deleted
    const char *taskname, states[] = "*RBSD";
    uint32_t ulTotalRunTime = 0;
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
    LOOPN(i, num) { LOOP(j, i + 1, num) { // sort tasks by specified attribute
        if (task_compare(sort_attr, tasks + i, tasks + j)) continue;
        tmp = tasks[i]; tasks[i] = tasks[j]; tasks[j] = tmp;
    } }
#   ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    LOOPN(i, num) { ulTotalRunTime += tasks[i].ulRunTimeCounter; }
#   endif
    printf("TID State Name            Pri CPU Used StackHW\n");
    LOOPN(i, num) {
        if (!strcmp(taskname = tasks[i].pcTaskName, "IDLE"))
            taskname = tasks[i].xCoreID ? "CPU 1 App" : "CPU 0 Pro";
        printf("%3d  (%c)  %-15s %2d  %3d %3d%% %7s\n",
               tasks[i].xTaskNumber, states[tasks[i].eCurrentState],
               taskname, tasks[i].uxCurrentPriority,
               tasks[i].xCoreID > 1 ? -1 : tasks[i].xCoreID,
               100 * tasks[i].ulRunTimeCounter / (ulTotalRunTime ?: 1),
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
    printf("ESP  IDF: %s\n"
           "FreeRTOS: %s\n"
           "Firmware: %s\n"
           "Compiled: %s %s\n",
           esp_get_idf_version(), tskKERNEL_VERSION_NUMBER,
           Config.info.VER, __DATE__, __TIME__);
}

void memory_info() {
    static const uint32_t caps[] = {
        MALLOC_CAP_DEFAULT, MALLOC_CAP_INTERNAL, MALLOC_CAP_SPIRAM,
        MALLOC_CAP_DMA, MALLOC_CAP_EXEC
    };
    static const char * const names[] = {
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
        printf("%-7s %8s ", names[i], format_size(total, false));
        printf("%8s ", format_size(tfree, false));
        printf("%3d%% ", total ? 100 * info.total_allocated_bytes / total : 0);
        printf("%3d%% ", tfree ? 100 * tfrag / tfree : 0);
        printf("0x%08x\n", caps[i]);
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
        switch (REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_VER_PKG)) {
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
        uint8_t buf[8] = { 0 };
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
    } else if (
        part->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT ||
        part->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS ||
        !strcmp(part->label, Config.sys.FS_PART)
    ) {
        filesys_info_t info;
        if (filesys_get_info(FILESYS_FLASH, &info) && info.total)
            return 100 * info.used / info.total;
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
