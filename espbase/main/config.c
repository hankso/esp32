/* 
 * File: config.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 21:49:38
 *
 */

#include "config.h"

#include "cJSON.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_partition.h"

#if __has_include("esp_idf_version.h")
#    include "esp_idf_version.h"
#    if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 0, 0)
#        define _NVS_ITER_LIST
#    endif
#endif

#define NAMESPACE_INFO "info"
#define NAMESPACE_CFG "config"

static const char * TAG = "Config";

// default values
config_t Config = {
    .net = {
        .STA_SSID  = "",
        .STA_PASS  = "",
        .STA_HOST  = "",
        .AP_SSID   = "espbase",
        .AP_PASS   = "16011106",
        .AP_HOST   = "10.0.2.1",
        .AP_AUTO   = "1",
        .AP_HIDE   = "0",
    },
    .web = {
        .WS_NAME   = "",
        .WS_PASS   = "",
        .HTTP_NAME = "",
        .HTTP_PASS = "",
        .DIR_DATA  = "/data/",
        .DIR_DOCS  = "/docs/",
        .DIR_ROOT  = "/www/",
    },
    .app = {
        .MDNS_RUN  = "1",
        .MDNS_HOST = "",
        .OTA_RUN   = "1",
        .OTA_URL   = "",
        .PROMPT    = "esp32> ",
    },
    .info = {
#ifdef PROJECT_NAME
        .NAME  = PROJECT_NAME,
#else
        .NAME  = "",
#endif
#ifdef PROJECT_VER
        .VER   = PROJECT_VER,
#else
        .VER   = "",
#endif
#ifdef CHIP_UID
        .UID   = CHIP_UID,
#else
        .UID   = "",
#endif
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
    {"net.sta.ssid",    &Config.net.STA_SSID,   NULL},
    {"net.sta.pass",    &Config.net.STA_PASS,   NULL},
    {"net.sta.host",    &Config.net.STA_HOST,   NULL},
    {"net.ap.ssid",     &Config.net.AP_SSID,    NULL},
    {"net.ap.pass",     &Config.net.AP_PASS,    NULL},
    {"net.ap.host",     &Config.net.AP_HOST,    NULL},
    {"net.ap.auto",     &Config.net.AP_AUTO,    NULL},
    {"net.ap.hide",     &Config.net.AP_HIDE,    NULL},

    {"web.ws.name",     &Config.web.WS_NAME,    NULL},
    {"web.ws.pass",     &Config.web.WS_PASS,    NULL},
    {"web.http.name",   &Config.web.HTTP_NAME,  NULL},
    {"web.http.pass",   &Config.web.HTTP_PASS,  NULL},
    {"web.path.data",   &Config.web.DIR_DATA,   NULL},
    {"web.path.docs",   &Config.web.DIR_DOCS,   NULL},
    {"web.path.root",   &Config.web.DIR_ROOT,   NULL},

    {"app.mdns.run",    &Config.app.MDNS_RUN,   NULL},
    {"app.mdns.host",   &Config.app.MDNS_HOST,  NULL},
    {"app.ota.run",     &Config.app.OTA_RUN,    NULL},
    {"app.ota.url",     &Config.app.OTA_URL,    NULL},
    {"app.prompt",      &Config.app.PROMPT,     NULL},
};

static config_entry_t rolst[] = {       // readonly entries
    {"uid",  &Config.info.UID,  NULL},
    {"name", &Config.info.NAME, NULL},
};

static uint16_t rwlen = LEN(rwlst);

/******************************************************************************
 * Configuration I/O
 */

static int16_t config_index(const char *key) {
    LOOPN(i, rwlen) {
        if (!strcmp(key, rwlst[i].key)) return i;
    }
    return -1;
}

static bool config_set_safe(config_entry_t *ent, const char *value) {
    if (!strcmp(*ent->value, value)) return true;
    char *tmp = strdup(value);
    if (!tmp) return false;
    TRYFREE(ent->freeval);
    *ent->value = ent->freeval = tmp;
    return true;
}

bool config_set(const char *key, const char *value) {
    int16_t idx = config_index(key);
    if (idx == -1) return false;
    return config_set_safe(rwlst + idx, value ? value : "");
}

const char * config_get(const char *key) {
    int16_t idx = config_index(key);
    return idx == -1 ? "Unknown" : *rwlst[idx].value;
}

void config_list() {
    printf("Namespace: " NAMESPACE_CFG "\n  KEY\t\t\tVALUE\n");
    LOOPN(i, rwlen) {
        const char *key = rwlst[i].key, *value = *rwlst[i].value;
        printf("  %-15.15s\t", key);
        if (!strcmp(key + strlen(key) - 4, "pass")) {
            printf("`%.*s`", strlen(value), "****************");
        } else {
            printf("`%s`", value);
        }
        printf("%s\n", rwlst[i].freeval ? " (modified)" : "");
    }
}

static void set_config_callback(const char *key, cJSON *item) {
    if (!cJSON_IsString(item)) {
        if (!cJSON_IsObject(item)) {
            ESP_LOGE(TAG, "Invalid type of `%s`", cJSON_Print(item));
        }
        return;
    }
    const char *val = item->valuestring;
    if (!config_set(key, val))
        ESP_LOGD(TAG, "JSON Config set `%s` to `%s` failed", key, val);
}

static void json_parse_object_recurse(
    cJSON *item, void (*cb)(const char *, cJSON *), const char *prefix)
{
    while (item) {
        if (item->string == NULL) {  // may be the root object
            if (item->child) json_parse_object_recurse(item->child, cb, prefix);
            item = item->next;
            continue;
        }

        // resolve key string with parent's name
        uint8_t plen = strlen(prefix);
        uint8_t slen = strlen(item->string) + (plen ? plen + 1 : 0) + 1;
        char *key = (char *)malloc(slen);
        if (key == NULL) continue;
        if (plen) snprintf(key, slen, "%s.%s", prefix, item->string);
        else      snprintf(key, slen, item->string);

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
    char *string = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return string;
}

/******************************************************************************
 * Configuration utilities
 */

static struct {
    bool init;                      // whether nvs flash has been initialized
    esp_err_t error;                // nvs flash init result
    nvs_handle handle;              // nvs handle obtained from nvs_open
    const esp_partition_t *part;    // nvs flash partition
} nvs_st = { false, ESP_OK, 0, NULL };

// nvs helper function: nvs_load_str wrapps on nvs_get_str
static esp_err_t nvs_load_str(config_entry_t *ent) {
    esp_err_t err;
    char *value;
    size_t len = 0;
    if (( err = nvs_get_str(nvs_st.handle, ent->key, NULL, &len) )) return err;
    if (!( value = (char *)malloc(len) )) return ESP_ERR_NO_MEM;
    if (!( err = nvs_get_str(nvs_st.handle, ent->key, value, &len) )) {
        if (config_set_safe(ent, value)) err = ESP_ERR_NO_MEM;
    }
    TRYFREE(value);
    return err;
}

esp_err_t config_nvs_init() {
    if (nvs_st.init) return nvs_st.error;
    nvs_st.part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    if (nvs_st.part == NULL) {
        ESP_LOGE(TAG, "Could not found nvs partition. Skip");
        nvs_st.init = true;
        return nvs_st.error = ESP_ERR_NOT_FOUND;
    }
#ifdef CONFIG_AUTOSTART_ARDUINO
    nvs_flash_deinit();
#endif
    esp_err_t err;
    bool enc = false;
#ifdef CONFIG_NVS_ENCRYPT
    enc = true;
    const esp_partition_t *keys = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_NVS_KEY, NULL);
    if (keys != NULL) {
        nvs_sec_cfg_t *cfg;
        err = nvs_flash_read_security_cfg(keys, cfg);
        if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
            err = nvs_flash_generate_keys(keys, cfg);
        }
        if (!err) {
            err = nvs_flash_secure_init_partition(nvs_st.part->label, cfg);
        } else {
            ESP_LOGE(TAG, "Could not initialize nvs with encryption: %s",
                     esp_err_to_name(err));
            enc = false;
        }
    } else { enc = false; }
#endif
    if (!enc) err = nvs_flash_init_partition(nvs_st.part->label);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase_partition(nvs_st.part->label);
        if (!err) { // partition cleared. we try again
#ifdef CONFIG_NVS_ENCRYPT
            if (enc) {
                err = nvs_flash_secure_init_partition(nvs_st.part->label, cfg);
            } else
#endif
            {
                err = nvs_flash_init_partition(nvs_st.part->label);
            }
        }
    }
    if (err) {
        ESP_LOGE(TAG, "Could not init nvs flash: %s", esp_err_to_name(err));
    }
    nvs_st.init = true;
    nvs_st.error = err;
    return err;
}

esp_err_t config_nvs_open(const char *ns, bool ro) {
    if (nvs_st.handle) return ESP_FAIL;
    esp_err_t err = ESP_OK;
    if (!nvs_st.init) err = config_nvs_init();
    if (!err) {
        err = nvs_open(ns, ro ? NVS_READONLY : NVS_READWRITE, &nvs_st.handle);
    }
    if (err) {
        ESP_LOGE(TAG, "Could not open nvs namespace `%s:` %s",
                 ns, esp_err_to_name(err));
    }
    return err;
}

esp_err_t config_nvs_commit() {
    if (!nvs_st.handle) return ESP_ERR_NVS_INVALID_HANDLE;
    esp_err_t err = nvs_commit(nvs_st.handle);
    if (err) ESP_LOGE(TAG, "commit fail: %s", esp_err_to_name(err));
    return err;
}

esp_err_t config_nvs_close() {
    if (!nvs_st.handle) return ESP_ERR_NVS_INVALID_HANDLE;
    esp_err_t err = config_nvs_commit();
    nvs_close(nvs_st.handle); nvs_st.handle = 0;
    return err;
}

bool config_nvs_remove(const char *key) {
    if (config_nvs_open(NAMESPACE_CFG, false)) return false;
    esp_err_t err = nvs_erase_key(nvs_st.handle, key);
    if (err) ESP_LOGE(TAG, "erase `%s` fail: %s", key, esp_err_to_name(err));
    return config_nvs_close() == ESP_OK;
}

bool config_nvs_clear() {
    if (config_nvs_open(NAMESPACE_CFG, false)) return false;
    esp_err_t err = nvs_erase_all(nvs_st.handle);
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
        if (nvs_set_str(nvs_st.handle, rwlst[i].key, *rwlst[i].value)) {
            success = false;
            break;
        }
    }
    return (config_nvs_close() == ESP_OK) && success;
}

void config_nvs_list(bool all) {
    if (nvs_st.part == NULL) {
        ESP_LOGE(TAG, "Could not found nvs partition. Skip");
        return;
    }
#ifdef _NVS_ITER_LIST
    const char *ns = all ? NULL : NAMESPACE_CFG;
    nvs_entry_info_t info;
    nvs_iterator_t iter = nvs_entry_find(nvs_st.part->label, ns, NVS_TYPE_ANY);
    if (!iter) {
        ESP_LOGE(TAG, "No entries found for namespace `%s` in parition `%s`",
                 all ? "all" : ns, nvs_st.part->label);
        return;
    }
#   ifdef CONFIG_AUTO_ALIGN
    size_t nslen = 0, keylen = 0;
    while (iter) {
        nvs_entry_info(iter, &info);
        iter = nvs_entry_next(iter);
        nslen = MAX(nslen, strlen(info.namespace_name));
        keylen = MAX(keylen, strlen(info.key));
    }
    nvs_release_iterator(iter);
    iter = nvs_entry_find(nvs_st.part->label, ns, NVS_TYPE_ANY);
#   else
    size_t nslen = 16, keylen = NVS_KEY_NAME_MAX_SIZE; // see nvs.h
#   endif
    if (all) {
        printf("%-*s %-*s Value\n", nslen, "Namespace", keylen, "Key");
    } else {
        printf("Namespace: %s\n  %-*s Value\n", ns, keylen, "Key");
    }
    while (iter) {
        nvs_entry_info(iter, &info);
        iter = nvs_entry_next(iter);
        if (all) {
            printf("%-*s %-*s ", nslen, info.namespace_name, keylen, info.key);
        } else {
            printf("  %-*s ", keylen, info.key);
        }
        if (strcmp(info.namespace_name, NAMESPACE_CFG)) {
            putchar('\n');
            continue;
        }
        const char *value = config_get(info.key);
        if (endswith(info.key, "pass")) {
            printf("`%.*s`\n", strlen(value), "********");
        } else {
            printf("`%s`\n", value);
        }
    }
    nvs_release_iterator(iter);
#else
    ESP_LOGE(TAG, "NVS Entries iteration not supported");
#endif
}

void config_nvs_stats() {
    if (nvs_st.part == NULL) {
        ESP_LOGE(TAG, "Could not found nvs partition. Skip");
        return;
    }
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(nvs_st.part->label, &nvs_stats);
    if (err) {
        ESP_LOGE(TAG, "Could not get nvs status: %s", esp_err_to_name(err));
        return;
    }
    printf(
        "NVS Partition Size: %s\n"
        "  Namespaces: %d\n"
        "  Entries: %d/%d (%.2f %% free)\n",
        format_size(nvs_st.part->size, false), nvs_stats.namespace_count,
        nvs_stats.used_entries, nvs_stats.total_entries,
        100.0 * nvs_stats.free_entries / nvs_stats.total_entries
    );
}

void config_initialize() {
    esp_err_t err;
    config_nvs_init();

    // load readonly values
    if (config_nvs_open(NAMESPACE_INFO, true) == ESP_OK) {
        LOOPN(i, LEN(rolst)) {
            if (( err = nvs_load_str(rolst + i) )) {
                ESP_LOGD(TAG, "get nvs `%s` failed: %s",
                        rolst[i].key, esp_err_to_name(err));
            }
        }
        config_nvs_close();
    }

    // startup times counter test
    if (config_nvs_open(NAMESPACE_INFO, false) == ESP_OK) {
        uint32_t counter = 0;
        if (( err = nvs_get_u32(nvs_st.handle, "counter", &counter) )) {
            ESP_LOGE(TAG, "get u32 `counter` fail: %s", esp_err_to_name(err));
        }
        counter++;
        ESP_LOGI(TAG, "Current run times: %u", counter);
        if (( err = nvs_set_u32(nvs_st.handle, "counter", counter) )) {
            ESP_LOGE(TAG, "set u32 `counter` fail: %s", esp_err_to_name(err));
        }
        config_nvs_close();
    }

    config_nvs_load();
}
