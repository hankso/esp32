/* 
 * File: config.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 21:49:38
 */

#include "config.h"

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#define NAMESPACE_INFO "info"
#define NAMESPACE_CFG "config"

static const char * TAG = "Config";

// default values
config_t Config = {
    .sys = {
        .DIR_DATA  = "/data/",
        .DIR_DOCS  = "/docs/",
        .DIR_HTML  = "/www/",
        .BTN_HIGH  = "0",
        .INT_EDGE  = "NEG",
        .USB_MODE  = "HID_DEVICE",
        .BT_MODE   = "BLE_HIDD",
        .BT_SCAN   = "1",
    },
    .net = {
        .STA_SSID  = "",
        .STA_PASS  = "",
        .STA_HOST  = "",
        .AP_SSID   = "espbase",
        .AP_PASS   = "16011106",
        .AP_HOST   = "10.0.2.1",
        .AP_HIDE   = "0",
        .AP_AUTO   = "1",
        .SC_AUTO   = "1",
    },
    .web = {
        .WS_NAME   = "",
        .WS_PASS   = "",
        .HTTP_NAME = "",
        .HTTP_PASS = "",
        .AUTH_BASE = "0",
    },
    .app = {
        .MDNS_RUN  = "1",
        .MDNS_HOST = "",
        .SNTP_RUN  = "1",
        .SNTP_HOST = "pool.ntp.org",
        .OTA_AUTO  = "1",
        .OTA_URL   = "",
        .TIMEZONE  = "CST-8",   // China Standard Time
        .PROMPT    = "esp32> ",
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
    const char *key;
    const char * *value;
    char * freeval;
} config_entry_t;

static config_entry_t rwlst[] = {       // read/write entries
    {"sys.dir.data",    &Config.sys.DIR_DATA,   NULL},
    {"sys.dir.docs",    &Config.sys.DIR_DOCS,   NULL},
    {"sys.dir.html",    &Config.sys.DIR_HTML,   NULL},
    {"sys.btn.high",    &Config.sys.BTN_HIGH,   NULL},
    {"sys.int.edge",    &Config.sys.INT_EDGE,   NULL},
    {"sys.usb.mode",    &Config.sys.USB_MODE,   NULL},
    {"sys.bt.mode",     &Config.sys.BT_MODE,    NULL},
    {"sys.bt.scan",     &Config.sys.BT_SCAN,    NULL},

    {"net.sta.ssid",    &Config.net.STA_SSID,   NULL},
    {"net.sta.pass",    &Config.net.STA_PASS,   NULL},
    {"net.sta.host",    &Config.net.STA_HOST,   NULL},
    {"net.ap.ssid",     &Config.net.AP_SSID,    NULL},
    {"net.ap.pass",     &Config.net.AP_PASS,    NULL},
    {"net.ap.host",     &Config.net.AP_HOST,    NULL},
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
    {"app.ota.auto",    &Config.app.OTA_AUTO,   NULL},
    {"app.ota.url",     &Config.app.OTA_URL,    NULL},
    {"app.timezone",    &Config.app.TIMEZONE,   NULL},
    {"app.prompt",      &Config.app.PROMPT,     NULL},
};

static config_entry_t rolst[] = {       // readonly entries
    {"name", &Config.info.NAME, NULL},
    {"ver",  &Config.info.VER,  NULL},
    {"uid",  &Config.info.UID,  NULL},
};

static const uint16_t rwlen = LEN(rwlst);

/******************************************************************************
 * Configuration I/O
 */

static struct {
    bool init;                      // whether nvs flash has been initialized
    esp_err_t error;                // nvs flash init result
    nvs_handle handle;              // nvs handle obtained from nvs_open
    const esp_partition_t *part;    // nvs flash partition
} ctx = { false, ESP_OK, 0, NULL };

static int16_t config_index(const char *key) {
    LOOPN(i, rwlen) {
        if (!strcmp(key, rwlst[i].key)) return i;
    }
    return -1;
}

static esp_err_t config_set_safe(
    config_entry_t *ent, const char *value, bool commit
) {
    if (!strcmp(*ent->value, value)) return ESP_OK;
    char *tmp = strdup(value);
    if (!tmp) return ESP_ERR_NO_MEM;
    TRYFREE(ent->freeval);
    *ent->value = ent->freeval = tmp;
    if (commit && !config_nvs_open(NAMESPACE_CFG, false)) {
        nvs_set_str(ctx.handle, ent->key, *ent->value);
        config_nvs_close();
    }
    return ESP_OK;
}

bool config_set(const char *key, const char *value) {
    int16_t idx = config_index(key);
    if (idx == -1) return false;
    return config_set_safe(rwlst + idx, value ? value : "", true) == ESP_OK;
}

const char * config_get(const char *key) {
    int16_t idx = config_index(key);
    return idx == -1 ? "Unknown" : *rwlst[idx].value;
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
            LPCHR('*', MIN(strlen(*rwlst[i].value), 16));
        } else {
            printf(*rwlst[i].value);
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
    if (!config_set(key, val))
        ESP_LOGD(TAG, "JSON Config set `%s` to `%s` failed", key, val);
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

bool config_loads(const char *json) {
    cJSON *obj = cJSON_Parse(json);
    if (!obj) {
        ESP_LOGE(TAG, "Could not parse JSON: %s", cJSON_GetErrorPtr());
        return false;
    } else {
        json_parse_object_recurse(obj, &set_config_callback, "");
        cJSON_Delete(obj);
        return true;
    }
}

char * config_dumps() {
    cJSON *obj = cJSON_CreateObject();
    LOOPN(i, rwlen) {
        cJSON_AddStringToObject(obj, rwlst[i].key, *rwlst[i].value);
    }
    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return json;
}

/******************************************************************************
 * Configuration utilities
 */

static esp_err_t nvs_load_str(config_entry_t *ent) {
    size_t len = 0;
    char *buf = NULL;
    esp_err_t err = nvs_get_str(ctx.handle, ent->key, NULL, &len);
    if (!err) err = EMALLOC(buf, len);
    if (!err) err = nvs_get_str(ctx.handle, ent->key, buf, &len);
    if (!err) err = config_set_safe(ent, buf, false);
    TRYFREE(buf);
    return err;
}

static esp_err_t nvs_load_val_ro(nvs_entry_info_t *info, char **vptr) {
    char *buf = NULL;
    size_t len = 16;
    uint8_t data[8];
    nvs_handle hdl;
    esp_err_t err = nvs_open(info->namespace_name, NVS_READONLY, &hdl);
    if (err) return err;
    if (info->type == NVS_TYPE_STR) {
        if (!err) err = nvs_get_str(hdl, info->key, NULL, &len);
        if (!err) err = EMALLOC(buf, len);
        if (!err) err = nvs_get_str(hdl, info->key, buf, &len);
    } else if (info->type == NVS_TYPE_BLOB) {
        if (!err) err = nvs_get_blob(hdl, info->key, NULL, &len);
        if (!err) err = EMALLOC(buf, len * 3);
        if (!err) err = nvs_get_blob(hdl, info->key, buf + len * 2, &len);
        if (!err) hexdumps((buf + len * 2), buf, len, MIN(32, len * 2));
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
                snprintf(buf, len, "%u", *(uint32_t *)data);
                break;
            case NVS_TYPE_I32:
                err = nvs_get_i32(hdl, info->key, (int32_t *)data);
                snprintf(buf, len, "%d", *(int32_t *)data);
                break;
            case NVS_TYPE_U64:
                err = nvs_get_u64(hdl, info->key, (uint64_t *)data);
                snprintf(buf, len, "%llu", *(uint64_t *)data);
                break;
            case NVS_TYPE_I64:
                err = nvs_get_i64(hdl, info->key, (int64_t *)data);
                snprintf(buf, len, "%lld", *(int64_t *)data);
                break;
            default: err = ESP_ERR_INVALID_STATE;
        }
    }
    if (hdl) nvs_close(hdl);
    if (err) TRYFREE(buf);
    *vptr = buf;
    return err;
}

esp_err_t config_nvs_init() {
    if (ctx.init) return ctx.error;
    ctx.part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    if (ctx.part == NULL) {
        ESP_LOGE(TAG, "Could not found nvs partition. Skip");
        ctx.init = true;
        return ctx.error = ESP_ERR_NOT_FOUND;
    }
#ifdef CONFIG_AUTOSTART_ARDUINO
    nvs_flash_deinit();
#endif
    esp_err_t err;
    bool enc = false;
#ifdef CONFIG_NVS_ENCRYPT
    enc = true;
    const esp_partition_t *keys = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEY, NULL);
    if (keys != NULL) {
        nvs_sec_cfg_t *cfg;
        err = nvs_flash_read_security_cfg(keys, cfg);
        if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
            err = nvs_flash_generate_keys(keys, cfg);
        }
        if (!err) {
            err = nvs_flash_secure_init_partition(ctx.part->label, cfg);
        } else {
            ESP_LOGE(TAG, "Could not initialize nvs with encryption: %s",
                     esp_err_to_name(err));
            enc = false;
        }
    } else { enc = false; }
#endif
    if (!enc) err = nvs_flash_init_partition(ctx.part->label);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase_partition(ctx.part->label);
        if (!err) { // partition cleared. we try again
#ifdef CONFIG_NVS_ENCRYPT
            if (enc) {
                err = nvs_flash_secure_init_partition(ctx.part->label, cfg);
            } else
#endif
            {
                err = nvs_flash_init_partition(ctx.part->label);
            }
        }
    }
    if (err) {
        ESP_LOGE(TAG, "Could not init nvs flash: %s", esp_err_to_name(err));
    }
    ctx.init = true;
    ctx.error = err;
    return err;
}

esp_err_t config_nvs_open(const char *ns, bool ro) {
    if (ctx.handle) return ESP_FAIL;
    esp_err_t err = ctx.init ? ESP_OK : config_nvs_init();
    if (!err) err = nvs_open(ns, ro ? NVS_READONLY : NVS_READWRITE, &ctx.handle);
    if (err) ESP_LOGE(TAG, "open `%s` fail: %s", ns, esp_err_to_name(err));
    return err;
}

esp_err_t config_nvs_commit() {
    if (!ctx.handle) return ESP_ERR_NVS_INVALID_HANDLE;
    esp_err_t err = nvs_commit(ctx.handle);
    if (err) ESP_LOGE(TAG, "commit fail: %s", esp_err_to_name(err));
    return err;
}

esp_err_t config_nvs_close() {
    if (!ctx.handle) return ESP_ERR_NVS_INVALID_HANDLE;
    esp_err_t err = config_nvs_commit();
    nvs_close(ctx.handle); ctx.handle = 0;
    return err;
}

bool config_nvs_remove(const char *key) {
    if (config_nvs_open(NAMESPACE_CFG, false)) return false;
    esp_err_t err = nvs_erase_key(ctx.handle, key);
    if (err) ESP_LOGE(TAG, "erase `%s` fail: %s", key, esp_err_to_name(err));
    return config_nvs_close() == ESP_OK;
}

bool config_nvs_clear() {
    if (config_nvs_open(NAMESPACE_CFG, false)) return false;
    esp_err_t err = nvs_erase_all(ctx.handle);
    if (err) ESP_LOGE(TAG, "erase nvs data fail: %s", esp_err_to_name(err));
    return config_nvs_close() == ESP_OK;
}

bool config_nvs_load() {
    esp_err_t err;
    if (config_nvs_open(NAMESPACE_CFG, true)) return false;
    LOOPN(i, rwlen) {
        if (( err = nvs_load_str(rwlst + i) )) {
            ESP_LOGD(TAG, "get nvs `%s` failed: %s",
                    rwlst[i].key, esp_err_to_name(err));
        }
    }
    return config_nvs_close() == ESP_OK;
}

bool config_nvs_dump() {
    if (config_nvs_open(NAMESPACE_CFG, false)) return false;
    bool success = true;
    LOOPN(i, rwlen) {
        if (nvs_set_str(ctx.handle, rwlst[i].key, *rwlst[i].value)) {
            success = false;
            break;
        }
    }
    return (config_nvs_close() == ESP_OK) && success;
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

void config_nvs_list(bool all) {
    if (ctx.part == NULL) {
        ESP_LOGE(TAG, "Could not found nvs partition. Skip");
        return;
    }
    const char *ns = all ? NULL : NAMESPACE_CFG, *val;
    nvs_entry_info_t info;
    nvs_iterator_t iter = nvs_entry_find(ctx.part->label, ns, NVS_TYPE_ANY);
    if (!iter) {
        ESP_LOGE(TAG, "No entries found for namespace `%s` in parition `%s`",
                 all ? "all" : ns, ctx.part->label);
        return;
    }
#   ifdef CONFIG_BASE_AUTO_ALIGN
    size_t nslen = 0, keylen = 0;
    while (iter) {
        nvs_entry_info(iter, &info);
        iter = nvs_entry_next(iter);
        nslen = MAX(nslen, strlen(info.namespace_name));
        keylen = MAX(keylen, strlen(info.key));
    }
    nvs_release_iterator(iter);
    iter = nvs_entry_find(ctx.part->label, ns, NVS_TYPE_ANY);
#   else
    size_t nslen = 16, keylen = NVS_KEY_NAME_MAX_SIZE; // see nvs.h
#   endif
    if (all) {
        printf("%-*s %-*s Type Value\n", nslen, "Namespace", keylen, "Key");
    } else {
        printf("Namespace: %s\n  %-*s Type Value\n", ns, keylen, "Key");
    }
    while (iter) {
        nvs_entry_info(iter, &info);
        iter = nvs_entry_next(iter);
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
    }
    nvs_release_iterator(iter);
}

void config_nvs_stats() {
    if (ctx.part == NULL) {
        ESP_LOGE(TAG, "Could not found nvs partition. Skip");
        return;
    }
    nvs_stats_t stat;
    esp_err_t err = nvs_get_stats(ctx.part->label, &stat);
    if (err) {
        ESP_LOGE(TAG, "Could not get nvs status: %s", esp_err_to_name(err));
        return;
    }
    printf(
        "NVS Partition Size: %s\n"
        "  Namespaces: %d\n"
        "  Entries: %d/%d (%.2f %% free)\n",
        format_size(ctx.part->size, false), stat.namespace_count,
        stat.used_entries, stat.total_entries,
        100.0 * stat.free_entries / (stat.total_entries ?: 1)
    );
}

void config_initialize() {
    esp_err_t err;
    config_nvs_init();
    config_nvs_load();

    if (strlen(Config.app.TIMEZONE)) {
        setenv("TZ", Config.app.TIMEZONE, 1);
        tzset();
    }

    // startup times counter test
    if (config_nvs_open(NAMESPACE_INFO, false) == ESP_OK) {
        uint32_t counter = 0;
        if (( err = nvs_get_u32(ctx.handle, "counter", &counter) ))
            ESP_LOGE(TAG, "get u32 `counter` fail: %s", esp_err_to_name(err));
        counter++;
        ESP_LOGI(TAG, "Current run times: %u", counter);
        if (( err = nvs_set_u32(ctx.handle, "counter", counter) ))
            ESP_LOGE(TAG, "set u32 `counter` fail: %s", esp_err_to_name(err));
        const esp_app_desc_t *desc = esp_ota_get_app_description();
        Config.info.NAME = desc->project_name;
        Config.info.VER = desc->version;
        LOOPN(i, LEN(rolst)) {
            if (( err = nvs_load_str(rolst + i) )) {
                ESP_LOGD(TAG, "get nvs `%s` failed: %s",
                        rolst[i].key, esp_err_to_name(err));
            }
        }
        config_nvs_close();
    }
}
