/*
 * File: btmode.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/13 7:12:40
 */

#include "btmode.h"
#include "config.h"

#include "esp_system.h" // for esp_restart

#ifdef CONFIG_BASE_USE_BT

#define ESP_ERR_BTMODE_BASE             0x600
#define ESP_ERR_BTMODE_DISABLED         ( ESP_ERR_BTMODE_BASE + 1 )
#define ESP_ERR_BTMODE_NOT_INITED       ( ESP_ERR_BTMODE_BASE + 2 )
#define ESP_ERR_BTMODE_PENDING_REBOOT   ( ESP_ERR_BTMODE_BASE + 3 )

static const char * TAG = "BTMode";
static int state = -ESP_ERR_BTMODE_NOT_INITED;

/*
 * BTMode APIs
 */

// Implemented in btdev.c
void btdev_status(btmode_t);
esp_err_t bt_hidd_init(btmode_t);
esp_err_t bt_hidd_exit(btmode_t);
esp_err_t ble_hidd_init(btmode_t);
esp_err_t ble_hidd_exit(btmode_t);

// Implemented in bthost.c
void bthost_status(btmode_t);
esp_err_t ble_hidh_init(btmode_t);
esp_err_t ble_hidh_exit(btmode_t);

static const struct {
    btmode_t mode;
    esp_err_t (*init)(btmode_t prev);
    esp_err_t (*exit)(btmode_t next);
} modes[] = {
    { BT_HIDD,  bt_hidd_init,   bt_hidd_exit },
    { BLE_HIDD, ble_hidd_init,  ble_hidd_exit },
    { BLE_HIDH, ble_hidh_init,  ble_hidh_exit },
};

static const char * btmode_str(btmode_t mode) {
    if (mode == -1 && state >= 0) mode = state;
    switch (mode) {
    CASESTR(BT_HIDD, 0);
    CASESTR(BLE_HIDD, 0);
    CASESTR(BLE_HIDH, 0);
    default: if (mode != -1) return "Unknown";
    }
    esp_err_t err = -state;
    switch (err) {
    case ESP_ERR_BTMODE_DISABLED:       return "Disabled";
    case ESP_ERR_BTMODE_NOT_INITED:     return "Uninitialized";
    case ESP_ERR_BTMODE_PENDING_REBOOT: return "Pending reboot";
    default:                            return esp_err_to_name(err);
    }
}

esp_err_t btmode_switch(btmode_t mode, bool reboot) {
    esp_err_t err = ESP_OK;
    if (mode == state) return err;
    bool exited = state == -ESP_ERR_BTMODE_DISABLED || \
                  state == -ESP_ERR_BTMODE_NOT_INITED;
    LOOPN(i, LEN(modes)) {
        if (mode != modes[i].mode) continue;
        LOOPN(j, LEN(modes)) {
            if (state != modes[j].mode || !modes[j].exit) continue;
            if (( err = modes[j].exit(mode) ) == ESP_OK) {
                exited = true;
            } else if (err != ESP_FAIL) {
                ESP_LOGE(TAG, "mode %s exit failed: %s",
                         btmode_str(state), esp_err_to_name(err));
            }
        }
        config_set("sys.bt.mode", btmode_str(mode));
        if (!exited) {
            if (reboot) esp_restart();
            state = -ESP_ERR_BTMODE_PENDING_REBOOT;
            ESP_LOGI(TAG, "mode set to %s (pending)", btmode_str(mode));
            return err;
        }
        if (!( err = modes[i].init ? modes[i].init(state) : ESP_OK )) {
            state = mode;
            ESP_LOGI(TAG, "mode set to %s", btmode_str(mode));
        } else {
            state = -err;
            ESP_LOGE(TAG, "mode set to %s failed: %s",
                     btmode_str(mode), esp_err_to_name(err));
        }
        return err;
    }
    ESP_LOGE(TAG, "Invalid mode %d: %s", mode, btmode_str(mode));
    return ESP_ERR_NOT_FOUND;
}

void btmode_initialize() {
    esp_log_level_set("BT_HCI", ESP_LOG_ERROR);
    LOOPN(i, LEN(modes)) {
        if (strcasecmp(btmode_str(modes[i].mode), Config.sys.BT_MODE)) continue;
        btmode_switch(modes[i].mode, false);
        return;
    }
    if (!strlen(Config.sys.BT_MODE)) {
        ESP_LOGW(TAG, "Software blocked");
        state = -ESP_ERR_BTMODE_DISABLED;
    } else {
        ESP_LOGE(TAG, "Unknonw mode. This should not happen!");
    }
}

void btmode_status() {
    printf("Current mode is %s (%d)\n", btmode_str(-1), ABS(state));
    if (ISSRV(state)) btdev_status(state);
    if (ISCLI(state)) bthost_status(state);
}

#else // CONFIG_BASE_USE_BT

esp_err_t btmode_switch(btmode_t m, bool r) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(m); NOTUSED(r);
}

void btmode_initialize() {}

void btmode_status() {}

#endif
