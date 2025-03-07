/* 
 * File: update.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-23 11:42:48
 */

#include "update.h"
#include "config.h"
#include "filesys.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_image_format.h"

static const char *TAG = "Update";

static struct {
    const esp_partition_t *target, *running;
    esp_ota_handle_t handle;
    esp_err_t error;
    size_t saved, total;
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
#if defined(CONFIG_APP_ROLLBACK_ENABLE) && !defined(CONFIG_AUTOSTART_ARDUINO)
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
#endif // CONFIG_APP_ROLLBACK_ENABLE
}

void ota_updation_reset() {
    ctx.handle = 0;
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
    if (!ctx.target || ctx.handle) return false;
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
        ctx.handle = 0;
        ESP_LOGE(TAG, "OTA init error: %s", ota_updation_error());
        return false;
    }
    return true;
}

bool ota_updation_write(void *data, size_t size) {
    if (!ctx.handle || ctx.error) return false;
    ctx.error = esp_ota_write(ctx.handle, data, size);
    if (ctx.error) {
        ESP_LOGE(TAG, "OTA write error: %s", ota_updation_error());
        return false;
    }
    ctx.saved += size;
    printf("\rProgress: %4d / %4d KB %3d%%",
           ctx.saved / 1024, ctx.total / 1024,
           100 * ctx.saved / (ctx.total ?: 1));
    return true;
}

bool ota_updation_end() {
    if (!ctx.handle) return false;
    putchar('\n'); // enter newline after ota_updation_write progress
    if (( ctx.error = esp_ota_end(ctx.handle) )) {
        ESP_LOGE(TAG, "OTA end error: %s", ota_updation_error());
    } else if (( ctx.error = esp_ota_set_boot_partition(ctx.target) )) {
        ESP_LOGI(TAG, "Set boot partition to %s error: %s",
                 ctx.target->label, ota_updation_error());
    } else {
        return true;
    }
    ctx.handle = 0;
    return false;
}

const char * ota_updation_error() {
    return ctx.error != ESP_OK ? esp_err_to_name(ctx.error) : NULL;
}

bool ota_updation_url(const char *url, bool force) {
#ifndef CONFIG_BASE_OTA_FETCH
    ctx.error = ESP_ERR_NOT_SUPPORTED;
    return false; NOTUSED(url); NOTUSED(force);
#else
    if (!ctx.target || ctx.handle) return false;
    if (!url) url = Config.app.OTA_URL;
    if (!strlen(url)) {
        ctx.error = ESP_ERR_INVALID_ARG;
        return false;
    }
    ESP_LOGI(TAG, "OTA Updation from URL `%s`", url);
    esp_app_desc_t new_info, ota_info, run_info;
    size_t
        hsize = sizeof(esp_image_header_t),
        ssize = sizeof(esp_image_segment_header_t),
        asize = sizeof(esp_app_desc_t),
        vsize = sizeof(new_info.version),
        tsize = sizeof(new_info.time);
    char buf[hsize + ssize + asize + 128], *cert = NULL;
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 2000,
        .keep_alive_enable = true,
    };
#   ifdef CONFIG_BASE_USE_FFS
    const char *fullpath = fjoin(2, Config.sys.DIR_DATA, "server.pem");
    config.cert_len = 4096; // skip files larger than 4KB
    config.cert_pem = cert = (char *)fload(fullpath, &config.cert_len);
    config.skip_cert_common_name_check = config.cert_pem != NULL;
#   endif
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ctx.error = ESP_FAIL;
        goto http_clean;
    }
    if (( ctx.error = esp_http_client_open(client, 0) )) goto http_clean;
    esp_http_client_fetch_headers(client);
    while (1) {
        int rsize = esp_http_client_read(client, buf, sizeof(buf));
        if (rsize < 0) {
            ESP_LOGE(TAG, "SSL data read error");
            goto ota_error;
        }
        if (rsize == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                if (ctx.handle) ESP_LOGD(TAG, "Firmware downloaded");
            } else if (errno) {
                ESP_LOGE(TAG, "Firmware downloading error: %d", errno);
                goto ota_error;
            }
            break;
        }
        if (!ctx.handle) {
            if (rsize < (hsize + ssize + asize)) {
                ESP_LOGE(TAG, "Received header does not fit length: %d", rsize);
                ctx.error = ESP_ERR_INVALID_SIZE;
                goto http_clean;
            }
            memcpy(&new_info, buf + hsize + ssize, asize);
            ESP_LOGI(TAG, "New app version: %s %s %s",
                     new_info.version, new_info.date, new_info.time);
            if (!esp_ota_get_partition_description(ctx.running, &run_info))
                ESP_LOGI(TAG, "Running version: %s %s %s",
                         run_info.version, run_info.date, run_info.time);
            if (!esp_ota_get_partition_description(ctx.target, &ota_info))
                ESP_LOGI(TAG, "Another version: %s %s %s",
                         ota_info.version, ota_info.date, ota_info.time);
            if (!force && !endswith(url, "?force") && (
                (
                    !memcmp(new_info.version, ota_info.version, vsize) &&
                    !memcmp(new_info.time, ota_info.time, tsize)
                ) || (
                    !memcmp(new_info.version, run_info.version, vsize) &&
                    !memcmp(new_info.time, run_info.time, tsize)
                )
            )) {
                ESP_LOGW(TAG, "New version app is already downloaded. Skip");
                ctx.error = ESP_ERR_INVALID_STATE;
                goto http_clean;
            }
            if (!ota_updation_begin(0)) goto http_clean;
        }
        if (!ota_updation_write(buf, rsize)) goto ota_error;
    }
    ota_updation_end();
    goto http_clean;

ota_error:
    if (!ctx.error) ctx.error = ESP_FAIL;
    if (ctx.handle) esp_ota_abort(ctx.handle);
http_clean:
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
            printf("%-8.7s" "%-8.7s" "%s\n",
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
        printf("%-8.7s" "%-8.7s" "0x%06X 0x%06X " "%-6.6s "
               "%-8.8s.. %7.7s %s %s\n",
               dstr, part->label, part->address, part->size,
               desc.idf_ver, format_sha256(desc.app_elf_sha256, 8),
               desc.version, desc.date, desc.time);
    }
    esp_partition_iterator_release(iter);
}
