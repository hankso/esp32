/* 
 * File: update.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-23 11:42:48
 */

#include "update.h"
#include "config.h"
#include "filesys.h"            // for filesys_xxx
#include "network.h"            // for wifi_sta_wait

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_image_format.h"

#if !__has_include("esp_delta_ota.h")
#   warning "Run `idf.py add-dependency espressif/esp_delta_ota`"
#else
#   include "esp_delta_ota.h"
#   define WITH_DELTA
#endif

static const char *TAG = "Update";

static struct {
    const esp_partition_t *target, *running;
#ifdef WITH_DELTA
    esp_delta_ota_handle_t delta;
#endif
    esp_ota_handle_t handle;
    size_t saved, total;
    esp_err_t error;
    bool check;
} ctx;

void update_initialize() {
    const esp_partition_t
        *running = esp_ota_get_running_partition(),
        *target = esp_ota_get_last_invalid_partition();
    if (!target) target = esp_ota_get_next_update_partition(running);
    if (target) {
        ctx.target = target;
        ota_updation_reset();
    } else {
        ctx.error = ESP_ERR_NOT_FOUND;
    }
    ctx.running = running;
#ifdef CONFIG_APP_ROLLBACK_ENABLE
    esp_image_metadata_t data;
    esp_ota_img_states_t state;
    const esp_partition_pos_t running_pos;
    if (esp_ota_get_state_partition(running, &state)) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;
    running_pos.offset = data.start_addr = running->address;
    running_pos.size = running->size;
    if (!esp_image_verify(ESP_IMAGE_VERIFY, &running_pos, &data)) {
        ESP_LOGW(TAG, "App validation success!");
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (!err) err = esp_ota_erase_last_boot_app_partition();
        if (err) {
            ESP_LOGE(TAG, "Anti-rollback error: %s", esp_err_to_name(err));
        } else {
            ESP_LOGE(TAG, "Last boot partition erased!");
        }
    } else if (esp_ota_check_rollback_is_possible()) {
        esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
        if (err) ESP_LOGE(TAG, "Could not rollback: %s", esp_err_to_name(err));
    } else {
        ESP_LOGE(TAG, "App validation failed!");
    }
#endif
}

void ota_updation_reset() {
#ifdef WITH_DELTA
    TRYNULL(ctx.delta, esp_delta_ota_deinit);
#endif
    TRYNULL(ctx.handle, esp_ota_end);
    ctx.check = false;
    ctx.error = ESP_OK;
    ctx.saved = 0;
    ctx.total = ctx.target ? ctx.target->size : OTA_SIZE_UNKNOWN;
}

bool ota_updation_boot(const char *label) {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label);
    if (!part) {
        ctx.error = ESP_ERR_NOT_FOUND;
    } else {
        ctx.error = esp_ota_set_boot_partition(part);
    }
    return ctx.error == ESP_OK;
}

bool ota_updation_begin(size_t size) {
    if (!ctx.target || ctx.handle) return ctx.handle;
    ota_updation_reset();
    if (size && size != OTA_SIZE_UNKNOWN && size != OTA_WITH_SEQUENTIAL_WRITES) {
        if (size > ctx.target->size) {
            ctx.error = ESP_ERR_INVALID_SIZE;
            return false;
        } else if (size == ctx.target->size) {
            // esp_ota_begin will erase (size/sector + 1) * sector
            // So we either make size one byte smaller than partition size
            // or set size to OTA_SIZE_UNKNOWN which means whole partition
            size--;
        } else {
            ctx.total = size;
        }
    }
    if (( ctx.error = esp_ota_begin(ctx.target, size, &ctx.handle) )) {
        ESP_LOGE(TAG, "OTA init error: %s", ota_updation_error());
        return false;
    }
    return true;
}

typedef struct {
    esp_image_header_t hdr;
    esp_image_segment_header_t seg;
    esp_app_desc_t app;
} PACKED header_normal_t;

typedef struct {
    uint32_t magic;
    uint8_t sha256[32];
    uint32_t length;
    uint8_t reserved[24];
} PACKED header_patched_t;

static esp_err_t verify_normal(header_normal_t *header) {
    if (header->hdr.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
        ESP_LOGE(TAG, "Received chip ID does not match: 0x%04X",
                 header->hdr.chip_id);
        return ESP_ERR_INVALID_VERSION;
    }
    ESP_LOGI(TAG, "New app version: %s %s %s",
             header->app.version, header->app.date, header->app.time);
    esp_app_desc_t rdesc, tdesc;
    if (!esp_ota_get_partition_description(ctx.running, &rdesc))
        ESP_LOGI(TAG, "Running version: %s %s %s",
                 rdesc.version, rdesc.date, rdesc.time);
    if (!esp_ota_get_partition_description(ctx.target, &tdesc))
        ESP_LOGI(TAG, "Old app version: %s %s %s",
                 tdesc.version, tdesc.date, tdesc.time);
    if ((!strcmp(header->app.version, tdesc.version) &&
         !strcmp(header->app.time,    tdesc.time) ) ||
        (!strcmp(header->app.version, rdesc.version) &&
         !strcmp(header->app.time,    rdesc.time))
    ) {
        ESP_LOGW(TAG, "New version is already downloaded. Skip");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

#ifdef WITH_DELTA
static esp_err_t verify_patched(header_patched_t *header) {
    uint8_t sha256[32];
    esp_err_t err = esp_partition_get_sha256(ctx.running, sha256);
    if (!err && memcmp(sha256, header->sha256, sizeof(sha256))) {
        ESP_LOGE(TAG, "Received patch is not for current firmware");
        err = ESP_ERR_INVALID_VERSION;
    }
    return err;
}

static esp_err_t delta_read(uint8_t *p, size_t s, int o) {
    if (s <= 0) return ESP_ERR_INVALID_ARG;
    return esp_partition_read(ctx.running, o, p, s);
}

#   if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
static esp_err_t delta_write(const uint8_t *data, size_t size, void *arg) {
#   else
static esp_err_t delta_write(const uint8_t *data, size_t size) {
#   endif
    if (size <= 0) return ESP_ERR_INVALID_ARG;
    if (!ctx.check) {
        static size_t saved = 0;
        static esp_image_header_t header;
        size_t want = sizeof(header) - saved;
        memcpy((uint8_t *)&header + saved, data, MIN(want, size));
        if (size < want) {
            saved += size;
            return ESP_OK;
        }
        saved = 0;
        ctx.check = true;
        // Firmware version validation can be skipped if
        // the sha256 matches current running firmware
        if (header.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
            ESP_LOGE(TAG, "Received chip ID does not match: 0x%04X",
                     header.chip_id);
            return ESP_ERR_INVALID_VERSION;
        }
        esp_err_t err = esp_ota_write(ctx.handle, &header, sizeof(header));
        if (err) return err;
    }
    return esp_ota_write(ctx.handle, data, size);
}
#else
static esp_err_t verify_patched(header_patched_t *h) {
    ESP_LOGE(TAG, "Patched firmware is not supported");
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(h);
}
#endif

bool ota_updation_write(void *data, size_t size) {
    if (ctx.error) return false;
    if (!ctx.handle && !ota_updation_begin(OTA_SIZE_UNKNOWN)) return false;
    if (!ctx.saved) {                   // first packet: parse header
        static size_t saved = 0;
        static union { header_normal_t normal; header_patched_t patched; } desc;
        size_t want = sizeof(desc) - saved;
        memcpy((uint8_t *)&desc + saved, data, MIN(size, want));
        if (size < want) {
            saved += size;
            return ESP_OK;
        }
        saved = 0;
        if (desc.patched.magic == 0xfccdde10) {
            if (( ctx.error = verify_patched(&desc.patched) )) return false;
#ifdef WITH_DELTA
            esp_delta_ota_cfg_t cfg = {
                .read_cb = delta_read,
#   if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
                .write_cb_with_user_data = delta_write,
#   else
                .write_cb = delta_write,
#   endif
            };
            if (!( ctx.delta = esp_delta_ota_init(&cfg) )) {
                ctx.error = ctx.error ?: ESP_FAIL;
                return false;
            }
            if (desc.patched.length) ctx.total = desc.patched.length;
#endif
        } else {
            size_t hlen = sizeof(header_normal_t);
            if (( ctx.error = verify_normal(&desc.normal) )) return false;
            if (( ctx.error = esp_ota_write(ctx.handle, &desc, hlen) )) return false;
        }
        size -= want;                   // data without header
        data = (uint8_t *)data + want;
    }
#ifdef WITH_DELTA
    if (ctx.delta) {
        ctx.error = esp_delta_ota_feed_patch(ctx.delta, data, size);
    } else
#endif
    {
        ctx.error = esp_ota_write(ctx.handle, data, size);
    }
    if (ctx.error) {
        ESP_LOGE(TAG, "OTA write error: %s", ota_updation_error());
        return false;
    }
    ctx.saved += size;
    fprintf(stderr, "\rProgress: %4d / %4d KB %3d%%",
            ctx.saved / 1024, ctx.total / 1024,
            100 * ctx.saved / (ctx.total ?: 1));
    fflush(stderr);
    return true;
}

bool ota_updation_end() {
    if (!ctx.handle) return true;
    fputc('\n', stderr);                // newline for progress info
#ifdef WITH_DELTA
    if (ctx.delta) {
        esp_err_t err = esp_delta_ota_finalize(ctx.delta);
        if (!err) err = esp_delta_ota_deinit(ctx.delta);
        if (!err) {
            ctx.delta = NULL;
        } else {
            ESP_LOGE(TAG, "Delta OTA deinit failed: %s", esp_err_to_name(err));
        }
    }
#endif
    if (( ctx.error = esp_ota_end(ctx.handle) )) {
        ESP_LOGE(TAG, "OTA end error: %s", ota_updation_error());
    } else if (( ctx.error = esp_ota_set_boot_partition(ctx.target) )) {
        ESP_LOGI(TAG, "Set boot partition to %s error: %s",
                 ctx.target->label, ota_updation_error());
    } else {
        ctx.handle = 0;
        return true;
    }
    return false;
}

const char * ota_updation_error() {
    return ctx.error != ESP_OK ? esp_err_to_name(ctx.error) : NULL;
}

bool ota_updation_url(const char *url) {
#ifndef CONFIG_BASE_OTA_FETCH
    ctx.error = ESP_ERR_NOT_SUPPORTED;
    return false; NOTUSED(url);
#else
    if (!ctx.target || ctx.handle) return false;
    if (!url) url = Config.app.OTA_URL;
    if (!strlen(url)) {
        ctx.error = ESP_ERR_INVALID_ARG;
        return false;
    }
    ESP_LOGI(TAG, "OTA Updation from URL `%s`", url);

    char buf[512], *cert = NULL, *end = NULL;
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 2000,
        .keep_alive_enable = true,
    };

#   ifdef CONFIG_BASE_USE_FFS
    const char *fullpath = fjoin(2, Config.sys.DIR_DATA, "server.pem");
    const char *stpl = "--BEGIN CERTIFICATE", *etpl = "END CERTIFICATE-----";
    config.cert_len = 4096;             // skip files larger than 4KB
    if (( cert = (char *)fload(fullpath, &config.cert_len) ) &&
        ( config.cert_pem = strstr(cert, stpl) ) &&
        ( end = strstr(config.cert_pem, etpl) )
    ) {
        config.cert_len = end + strlen(etpl) - config.cert_pem;
        config.skip_cert_common_name_check = true;
    } else {
        config.cert_len = 0;
        config.cert_pem = NULL;
    }
#   endif

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ctx.error = ESP_ERR_NO_MEM;
        goto exit;
    }
    if (( ctx.error = esp_http_client_open(client, 0) )) goto exit;
    esp_http_client_fetch_headers(client);
    while (1) {
        int ret = esp_http_client_read(client, buf, sizeof(buf));
        if (ret < 0) {
            ESP_LOGE(TAG, "Firmware download error: %s", esp_err_to_name(-ret));
            ctx.error = -ret;
            break;
        } else if (ret == 0) {
            int error = esp_http_client_get_errno(client);
            if (esp_http_client_is_complete_data_received(client)) {
                if (ctx.handle) ESP_LOGD(TAG, "Firmware downloaded");
            } else if (error && error != -1) {
                ESP_LOGE(TAG, "Firmware download error: %s", errstr(error));
                ctx.error = errval(error);
            }
            break;
        } else if (!ota_updation_write(buf, ret)) {
            ESP_LOGE(TAG, "Firmware write error: %s", ota_updation_error());
            break;
        }
    }
    ota_updation_end();
exit:
    TRYFREE(cert);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ctx.error == ESP_OK;
#endif // CONFIG_BASE_OTA_FETCH
}

static const char * const ota_img_states[] = {
    "New", "Pending", "Valid", "Invalid", "Aborted",
    [5] = "Unknown",
    [6] = "Running",
    [7] = "OTANext",
};

void ota_updation_info() {
    esp_partition_iterator_t iter = esp_partition_find(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (!iter) {
        ESP_LOGW(TAG, "No OTA partition found. Skip");
        return;
    }
    printf("State   Label   Offset   Size     IDF    "
           "SHA256[:8] Version Build time\n");
    esp_err_t err;
    esp_app_desc_t desc;
    esp_ota_img_states_t state;
    const char *dstr;
    const esp_partition_t *part, *boot = esp_ota_get_boot_partition();
    if (!boot) boot = ctx.running;
    while (iter) {
        part = esp_partition_get(iter);
        iter = esp_partition_next(iter);
        if (( err = esp_ota_get_partition_description(part, &desc) )) {
            printf("%-7s %-7.7s %s\n",
                   ota_img_states[3], part->label, esp_err_to_name(err));
            continue;
        }
        if (part->address == boot->address) {
            dstr = boot->address == ctx.running->address ? "Boot *" : "Boot";
        } else if (part->address == ctx.running->address) {
            dstr = ota_img_states[6];
        } else if (part->address == (ctx.target ? ctx.target->address : 0)) {
            dstr = ota_img_states[7];
        } else if (esp_ota_get_state_partition(part, &state) || state > 5) {
            dstr = ota_img_states[5];
        } else {
            dstr = ota_img_states[state];
        }
        printf("%-7s %-7.7s 0x%06" PRIX32 " 0x%06" PRIX32 " %-6.6s "
               "%-8.8s.. %7.7s %s %s\n",
               dstr, part->label, part->address, part->size,
               desc.idf_ver, format_sha256(desc.app_elf_sha256, 8),
               desc.version, desc.date, desc.time);
    }
    esp_partition_iterator_release(iter);
}
