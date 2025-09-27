/* 
 * File: config.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 21:49:38
 */

#include "config.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "cJSON.h"

#define NAMESPACE_INFO "info"
#define NAMESPACE_CFG "config"

static const char * TAG = "Config";

// default values
config_t Config = {
    .sys = {
        .TIMEZONE  = "CST-8",   // China Standard Time
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        .PROMPT    = "esp32s3> ",
#elif defined(CONFIG_IDF_TARGET_ESP32)
        .PROMPT    = "esp32> ",
#else
        .PROMPT    = "esp> ",
#endif
        .DIR_DATA  = "/data/",
        .DIR_DOCS  = "/docs/",
        .DIR_HTML  = "/www/",
        .BTN_HIGH  = "n",
        .INT_EDGE  = "ANY",
        .ADC_MULT  = "16",
        .USB_MODE  = "SERIAL_JTAG",
        .BT_MODE   = "BLE_HIDD",
        .BT_SCAN   = "y",
    },
    .net = {
        .ETH_HOST  = "",
        .ETH_GATE  = "",
        .STA_SSID  = "",
        .STA_PASS  = "",
        .STA_HOST  = "",
        .STA_GATE  = "",
        .AP_SSID   = "",
        .AP_PASS   = "",
        .AP_HOST   = "10.0.2.1",
        .AP_CHAN   = "1",
        .AP_NCON   = "4",
        .AP_NAPT   = "y",
        .AP_HIDE   = "n",
        .AP_AUTO   = "y",
        .SC_AUTO   = "y",
    },
    .web = {
        .WS_NAME   = "",
        .WS_PASS   = "",
        .HTTP_NAME = "",
        .HTTP_PASS = "",
        .AUTH_BASE = "n",
    },
    .app = {
        .MDNS_RUN  = "y",
        .MDNS_HOST = "",
        .SNTP_RUN  = "y",
        .SNTP_HOST = "pool.ntp.org",
        .TSCN_MODE = "REL",
        .HID_MODE  = "GENERAL",
        .HID_HOST  = "10.0.2.255",
        .HBT_AUTO  = "n",
        .HBT_URL   = "",
        .OTA_AUTO  = "y",
        .OTA_URL   = "",
    },
    .info = {
        .NAME      = "",
        .VER       = "",
        .UID       = "",
    }
};

// The rwlst is a mapping to flattened Configuration attributes.
// Low level `config_get/config_set` are actually manipulation on this array.
// Therefore, global variable `Config` is just a link to the rwlst memory.
//
// Config values are pointers to constant strings `const char *`, which should
// not be deallocated. Field `freeval` is designed to hold strdup of new value.
// Feel free to call `config_set` on `char *` (without memory leak).
typedef struct {
    const char * key;
    const char ** val;
    char * freeval;
} config_entry_t;

static config_entry_t rwlst[] = {       // read/write entries
    {"sys.timezone",    &Config.sys.TIMEZONE,   NULL},
    {"sys.prompt",      &Config.sys.PROMPT,     NULL},
    {"sys.dir.data",    &Config.sys.DIR_DATA,   NULL},
    {"sys.dir.docs",    &Config.sys.DIR_DOCS,   NULL},
    {"sys.dir.html",    &Config.sys.DIR_HTML,   NULL},
    {"sys.btn.high",    &Config.sys.BTN_HIGH,   NULL},
    {"sys.int.edge",    &Config.sys.INT_EDGE,   NULL},
    {"sys.adc.mult",    &Config.sys.ADC_MULT,   NULL},
    {"sys.usb.mode",    &Config.sys.USB_MODE,   NULL},
    {"sys.bt.mode",     &Config.sys.BT_MODE,    NULL},
    {"sys.bt.scan",     &Config.sys.BT_SCAN,    NULL},

    {"net.eth.host",    &Config.net.ETH_HOST,   NULL},
    {"net.eth.gate",    &Config.net.ETH_GATE,   NULL},
    {"net.sta.ssid",    &Config.net.STA_SSID,   NULL},
    {"net.sta.pass",    &Config.net.STA_PASS,   NULL},
    {"net.sta.host",    &Config.net.STA_HOST,   NULL},
    {"net.sta.gate",    &Config.net.STA_GATE,   NULL},
    {"net.ap.ssid",     &Config.net.AP_SSID,    NULL},
    {"net.ap.pass",     &Config.net.AP_PASS,    NULL},
    {"net.ap.host",     &Config.net.AP_HOST,    NULL},
    {"net.ap.chan",     &Config.net.AP_CHAN,    NULL},
    {"net.ap.ncon",     &Config.net.AP_NCON,    NULL},
    {"net.ap.napt",     &Config.net.AP_NAPT,    NULL},
    {"net.ap.hide",     &Config.net.AP_HIDE,    NULL},
    {"net.ap.auto",     &Config.net.AP_AUTO,    NULL},
    {"net.sc.auto",     &Config.net.SC_AUTO,    NULL},

    {"web.ws.name",     &Config.web.WS_NAME,    NULL},
    {"web.ws.pass",     &Config.web.WS_PASS,    NULL},
    {"web.http.name",   &Config.web.HTTP_NAME,  NULL},
    {"web.http.pass",   &Config.web.HTTP_PASS,  NULL},
    {"web.auth.base",   &Config.web.AUTH_BASE,  NULL},

    {"app.mdns.run",    &Config.app.MDNS_RUN,   NULL},
    {"app.mdns.host",   &Config.app.MDNS_HOST,  NULL},
    {"app.sntp.run",    &Config.app.SNTP_RUN,   NULL},
    {"app.sntp.host",   &Config.app.SNTP_HOST,  NULL},
    {"app.tscn.mode",   &Config.app.TSCN_MODE,  NULL},
    {"app.hid.mode",    &Config.app.HID_MODE,   NULL},
    {"app.hid.host",    &Config.app.HID_HOST,   NULL},
    {"app.hbt.auto",    &Config.app.HBT_AUTO,   NULL},
    {"app.hbt.url",     &Config.app.HBT_URL,    NULL},
    {"app.ota.auto",    &Config.app.OTA_AUTO,   NULL},
    {"app.ota.url",     &Config.app.OTA_URL,    NULL},
};

static config_entry_t rolst[] = {       // readonly entries
    {"name", &Config.info.NAME, NULL},
    {"ver",  &Config.info.VER,  NULL},
    {"uid",  &Config.info.UID,  NULL},
};

static uint16_t rwlen = LEN(rwlst);

/*
 * Configuration I/O
 */

ESP_EVENT_DEFINE_BASE(CFG_EVENT);

static struct {
    nvs_handle_t hdl;
    const esp_partition_t *part;
} ctx;

static int16_t config_index(const char *key) {
    LOOPN(i, rwlen) {
        if (!strcmp(key, rwlst[i].key)) return i;
    }
    return -1;
}

static esp_err_t config_set_safe(
    config_entry_t *ent, const char *value, bool commit
) {
    setenv(ent->key, value, true);
    if (!strcmp(*ent->val, value)) return ESP_OK;
    if (strlen(value)) {
        char *tmp = strdup(value);
        if (!tmp) return ESP_ERR_NO_MEM;
        TRYFREE(ent->freeval);
        *ent->val = ent->freeval = tmp;
    } else {
        TRYFREE(ent->freeval);
        *ent->val = "";
    }
    if (!commit) return ESP_OK;
    esp_err_t err = nvs_set_str(ctx.hdl, ent->key, *ent->val);
    if (!err) err = nvs_commit(ctx.hdl);
    return err;
}

esp_err_t config_set(const char *key, const char *value) {
    esp_err_t err = ESP_ERR_INVALID_ARG;
    int16_t idx = config_index(key);
    if (idx >= 0 && !( err = config_set_safe(rwlst + idx, value ?: "", true) ))
        esp_event_post(CFG_EVENT, CFG_UPDATE, (void *)key, strlen(key), 0);
    return err;
}

const char * config_get(const char *key) {
    int16_t idx = config_index(key);
    return idx == -1 ? "Unknown" : *rwlst[idx].val;
}

void config_stats() {
#ifdef CONFIG_BASE_AUTO_ALIGN
    size_t keylen = 0;
    LOOPN(i, rwlen) { keylen = MAX(keylen, strlen(rwlst[i].key)); }
#else
    size_t keylen = 16;
#endif
    printf("Namespace: " NAMESPACE_CFG "\n  %-*s Value\n", keylen, "Key");
    LOOPN(i, rwlen) {
        printf("  %-*s ", keylen, rwlst[i].key);
        if (endswith(rwlst[i].key, "pass")) {
            LPCHR('*', MIN(strlen(*rwlst[i].val), 16));
        } else {
            printf(*rwlst[i].val);
        }
        printf("%s\n", rwlst[i].freeval ? " (modified)" : "");
    }
}

static void set_config_callback(const char *key, cJSON *item) {
    const char *val;
    if (cJSON_IsNumber(item)) {
        val = item->valuedouble ? "1" : "0";
    } else if (cJSON_IsString(item)) {
        val = item->valuestring;
    } else {
        char *json = cJSON_Print(item);
        if (json) ESP_LOGE(TAG, "Invalid type of `%s`", json);
        TRYFREE(json);
        return;
    }
    if (config_set(key, val) != ESP_OK)
        ESP_LOGD(TAG, "Update `%s` to `%s` failed", key, val);
}

static void json_parse_object_recurse(
    cJSON *item, void (*cb)(const char *, cJSON *), const char *root
) {
    while (item) {
        if (item->string == NULL) {  // may be the root object or array
            if (item->child) json_parse_object_recurse(item->child, cb, root);
            item = item->next;
            continue;
        }

        // resolve key string with parent's name
        char *key;
        uint8_t rlen = strlen(root);
        uint8_t slen = strlen(item->string) + (rlen ? rlen + 1 : 0) + 1;
        if (EMALLOC(key, slen)) continue;
        snprintf(key, slen, "%s%s%s", root, rlen ? "." : "", item->string);

        (*cb)(key, item);

        // recurse to child and iterate to next sibling
        if (item->child) json_parse_object_recurse(item->child, cb, key);
        item = item->next;
        TRYFREE(key);
    }
}

esp_err_t config_loads(const char *json) {
    cJSON *obj = cJSON_Parse(json);
    if (!obj) {
        ESP_LOGE(TAG, "Could not parse JSON: %s", cJSON_GetErrorPtr());
        return ESP_ERR_INVALID_ARG;
    }
    json_parse_object_recurse(obj, &set_config_callback, "");
    cJSON_Delete(obj);
    return ESP_OK;
}

char * config_dumps() {
    cJSON *obj = cJSON_CreateObject();
    LOOPN(i, rwlen) {
        cJSON_AddStringToObject(obj, rwlst[i].key, *rwlst[i].val);
    }
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return json;
}

/*
 * Configuration utilities
 */

static esp_err_t nvs_load_str(nvs_handle_t hdl, config_entry_t *ent) {
    size_t len = 0;
    char *buf = NULL;
    esp_err_t err = nvs_get_str(hdl, ent->key, NULL, &len);
    if (!err) err = EMALLOC(buf, len);
    if (!err) err = nvs_get_str(hdl, ent->key, buf, &len);
    if (!err) err = config_set_safe(ent, buf, false);
    TRYFREE(buf);
    return err;
}

static esp_err_t nvs_load_val_ro(nvs_entry_info_t *info, char **vptr) {
    char *buf = NULL;
    size_t len = 16, off = 0;
    uint8_t data[8];
    nvs_handle_t hdl;
    esp_err_t err = nvs_open(info->namespace_name, NVS_READONLY, &hdl);
    if (err) return err;
    if (info->type == NVS_TYPE_STR) {
        if (!err) err = nvs_get_str(hdl, info->key, NULL, &len);
        if (!err) err = EMALLOC(buf, len);
        if (!err) err = nvs_get_str(hdl, info->key, buf, &len);
    } else if (info->type == NVS_TYPE_BLOB) {
        if (!err) err = nvs_get_blob(hdl, info->key, NULL, &len);
        if (!err) err = EMALLOC(buf, len + (off = MIN(40, len * 2 + 1)));
        if (!err) err = nvs_get_blob(hdl, info->key, buf + off, &len);
        if (!err) hexdumps(buf + off, buf, len, off);
    } else if (!( err = EMALLOC(buf, len) )) {
        switch (info->type) {
            case NVS_TYPE_U8:
                err = nvs_get_u8(hdl, info->key, (uint8_t *)data);
                snprintf(buf, len, "%u", *(uint8_t *)data);
                break;
            case NVS_TYPE_I8:
                err = nvs_get_i8(hdl, info->key, (int8_t *)data);
                snprintf(buf, len, "%d", *(int8_t *)data);
                break;
            case NVS_TYPE_U16:
                err = nvs_get_u16(hdl, info->key, (uint16_t *)data);
                snprintf(buf, len, "%u", *(uint16_t *)data);
                break;
            case NVS_TYPE_I16:
                err = nvs_get_i16(hdl, info->key, (int16_t *)data);
                snprintf(buf, len, "%d", *(int16_t *)data);
                break;
            case NVS_TYPE_U32:
                err = nvs_get_u32(hdl, info->key, (uint32_t *)data);
                snprintf(buf, len, "%" PRIu32, *(uint32_t *)data);
                break;
            case NVS_TYPE_I32:
                err = nvs_get_i32(hdl, info->key, (int32_t *)data);
                snprintf(buf, len, "%" PRId32, *(int32_t *)data);
                break;
            case NVS_TYPE_U64:
                err = nvs_get_u64(hdl, info->key, (uint64_t *)data);
                snprintf(buf, len, "%" PRIu64, *(uint64_t *)data);
                break;
            case NVS_TYPE_I64:
                err = nvs_get_i64(hdl, info->key, (int64_t *)data);
                snprintf(buf, len, "%" PRId64, *(int64_t *)data);
                break;
            default: err = ESP_ERR_INVALID_STATE;
        }
    }
    if (hdl) nvs_close(hdl);
    if (err) TRYFREE(buf);
    *vptr = buf;
    return err;
}

static esp_err_t config_nvs_init() {
    if (ctx.hdl) return ESP_OK;
    if (!( ctx.part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS,
        NVS_DEFAULT_PART_NAME
    ) )) return ESP_ERR_NOT_FOUND;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        err = nvs_flash_erase() ?: nvs_flash_init();
    if (!err) err = nvs_open(NAMESPACE_CFG, NVS_READWRITE, &ctx.hdl);
    return err;
}

esp_err_t config_nvs_open(void **pptr, const char *ns, bool ro) {
    if (!pptr || !ns) return ESP_ERR_INVALID_ARG;
    config_nvs_close(pptr);
    nvs_handle_t *hptr = (nvs_handle_t *)pptr;
    esp_err_t err = config_nvs_init();
    if (!err) err = nvs_open(ns, ro ? NVS_READONLY : NVS_READWRITE, hptr);
    if (err) *pptr = NULL;
    return err;
}

int config_nvs_read(void *ptr, const char *key, void *buf, size_t size) {
    if (!key || !buf || !size) return -ESP_ERR_INVALID_ARG;
    size_t len;
    nvs_handle_t hdl = (nvs_handle_t)ptr;
    esp_err_t err = nvs_get_blob(hdl, key, NULL, &len);
    if (!err) err = (len && len <= size) ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (!err) err = nvs_get_blob(hdl, key, buf, &len);
    return err ? -err : len;
}

int config_nvs_write(void *ptr, const char *key, const void *val, size_t len) {
    if (!key || !val || !len) return -ESP_ERR_INVALID_ARG;
    nvs_handle_t hdl = (nvs_handle_t)ptr;
    esp_err_t err = nvs_set_blob(hdl, key, val, len);
    if (!err) err = nvs_commit(hdl);
    return err ? -err : len;
}

esp_err_t config_nvs_delete(void *ptr, const char *key) {
    nvs_handle_t hdl = (nvs_handle_t)ptr;
    return key ? nvs_erase_key(hdl, key) : nvs_erase_all(hdl);
}

esp_err_t config_nvs_close(void **pptr) {
    if (!pptr) return ESP_ERR_INVALID_ARG;
    nvs_handle_t *hptr = (nvs_handle_t *)pptr;
    esp_err_t err = nvs_commit(*hptr);
    TRYNULL(*hptr, nvs_close);
    return err;
}

esp_err_t config_nvs_load() {
    esp_err_t err = config_nvs_init();
    if (err) return err;
    LOOPN(i, rwlen) {
        if (!( err = nvs_load_str(ctx.hdl, rwlst + i) )) continue;
        ESP_LOGD(TAG, "Get `%s` failed: %s", rwlst[i].key, esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t config_nvs_dump() {
    esp_err_t err = config_nvs_init();
    if (err) return err;
    LOOPN(i, rwlen) {
        if (!( err = nvs_set_str(ctx.hdl, rwlst[i].key, *rwlst[i].val) ))
            continue;
        ESP_LOGW(TAG, "Set `%s` failed: %s", rwlst[i].key, esp_err_to_name(err));
    }
    return err;
}

static const char * nvs_type_str(nvs_type_t type) {
    switch (type) {
        case NVS_TYPE_U8:   return "U8";
        case NVS_TYPE_I8:   return "I8";
        case NVS_TYPE_U16:  return "U16";
        case NVS_TYPE_I16:  return "I16";
        case NVS_TYPE_U32:  return "U32";
        case NVS_TYPE_I32:  return "I32";
        case NVS_TYPE_U64:  return "U64";
        case NVS_TYPE_I64:  return "I64";
        case NVS_TYPE_STR:  return "STR";
        case NVS_TYPE_BLOB: return "BLOB";
        case NVS_TYPE_ANY:  return "ANY";
        default:            return "Unknown";
    }
}

#ifdef IDF_TARGET_V4
#   define NVS_ITER_INIT(it, ...)   ( (it) = nvs_entry_find(__VA_ARGS__) )
#   define NVS_ITER_NEXT(it)        ( (it) = nvs_entry_next(it) )
#else
#   define NVS_ITER_INIT(it, ...)   ({ nvs_entry_find(__VA_ARGS__, &(it)); (it); })
#   define NVS_ITER_NEXT(it)        ({ nvs_entry_next(&(it)); (it); })
#endif

void config_nvs_list(bool all) {
    if (config_nvs_init()) {
        ESP_LOGE(TAG, "NVS init failed");
        return;
    }
    if (all) {
        nvs_stats_t stat;
        esp_err_t err = nvs_get_stats(ctx.part->label, &stat);
        if (err) {
            ESP_LOGW(TAG, "Failed to stat nvs: %s", esp_err_to_name(err));
        } else {
            printf(
                "NVS Partition Size: %s\n"
                "  Namespaces: %d\n"
                "  Entries: %d/%d (%.2f %% free)\n\n",
                format_size(ctx.part->size), stat.namespace_count,
                stat.used_entries, stat.total_entries,
                100.0 * stat.free_entries / (stat.total_entries ?: 1)
            );
        }
    }
    const char *ns = all ? NULL : NAMESPACE_CFG, *val;
    nvs_entry_info_t info;
    nvs_iterator_t iter = NULL;
    if (!NVS_ITER_INIT(iter, ctx.part->label, ns, NVS_TYPE_ANY)) {
        ESP_LOGE(TAG, "No entries found for namespace `%s`", all ? "all" : ns);
        return;
    }
#ifdef CONFIG_BASE_AUTO_ALIGN
    size_t nslen = 0, keylen = 0;
    while (iter) {
        nvs_entry_info(iter, &info);
        nslen = MAX(nslen, strlen(info.namespace_name));
        keylen = MAX(keylen, strlen(info.key));
        NVS_ITER_NEXT(iter);
    }
    TRYNULL(iter, nvs_release_iterator);
    NVS_ITER_INIT(iter, ctx.part->label, ns, NVS_TYPE_ANY);
#else
    size_t nslen = 16, keylen = NVS_KEY_NAME_MAX_SIZE; // see nvs.h
#endif
    if (all) {
        printf("%-*s %-*s Type Value\n", nslen, "Namespace", keylen, "Key");
    } else {
        printf("Namespace: %s\n  %-*s Type Value\n", ns, keylen, "Key");
    }
    while (iter) {
        nvs_entry_info(iter, &info);
        char *tmp = NULL;
        if (!strcmp(info.namespace_name, NAMESPACE_CFG)) {
            val = config_get(info.key);
        } else {
            val = nvs_load_val_ro(&info, &tmp) ? "" : tmp;
        }
        printf("%-*s %-*s %4s ",
                all ? nslen : 1, all ? info.namespace_name : " ",
                keylen, info.key, nvs_type_str(info.type));
        if (endswith(info.key, "pass") || endswith(info.key, "pswd")) {
            LPCHRN('*', MIN(strlen(val), 16));
        } else {
            puts(val);
        }
        TRYFREE(tmp);
        NVS_ITER_NEXT(iter);
    }
    nvs_release_iterator(iter);
}

void config_initialize() {
    nvs_handle_t info;
    esp_err_t err = config_nvs_load();
    if (!err) err = config_nvs_open((void **)&info, NAMESPACE_INFO, false);
    if (err) {
        ESP_LOGE(TAG, "Failed to init config: %s", esp_err_to_name(err));
        return;
    }
    // startup times counter test
    uint32_t counter = 0;
    if (( err = nvs_get_u32(info, "counter", &counter) ))
        ESP_LOGE(TAG, "Get `counter` failed: %s", esp_err_to_name(err));
    counter++;
    ESP_LOGI(TAG, "Current run times: %" PRIu32, counter);
    if (( err = nvs_set_u32(info, "counter", counter) ))
        ESP_LOGE(TAG, "Set `counter` failed: %s", esp_err_to_name(err));
#ifdef IDF_TARGET_V4
    const esp_app_desc_t *desc = esp_ota_get_app_description();
#else
    const esp_app_desc_t *desc = esp_app_get_description();
#endif
    Config.info.NAME = desc->project_name;
    Config.info.VER = desc->version;
    LOOPN(i, LEN(rolst)) {
        if (!( err = nvs_load_str(info, rolst + i) )) continue;
        ESP_LOGD(TAG, "Get `%s` failed: %s",
                 rolst[i].key, esp_err_to_name(err));
    }
    config_nvs_close((void **)&info);
    if (strlen(Config.sys.TIMEZONE)) {
        setenv("TZ", Config.sys.TIMEZONE, true);
        tzset();
    }
    esp_event_loop_create_default();
}
