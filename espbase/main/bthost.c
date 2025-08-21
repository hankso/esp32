/*
 * File: bthost.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/30 0:42:40
 */

#include "btmode.h"

#ifdef CONFIG_BASE_USE_BT

#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_hidh.h"

#define BDASTR  ESP_BD_ADDR_STR
#define BDA2STR ESP_BD_ADDR_HEX

// defined in btdev.c
esp_err_t bt_common_init(esp_bt_mode_t, bool);
esp_err_t bt_common_exit(bool);

#ifdef CONFIG_BASE_BLE_HID_HOST

static const char *BLE = "BLE HIDH";
static bool hid_enabled = false;
static esp_hidh_dev_t *hiddev = NULL;

static void ble_hidh_cb(void *a, esp_event_base_t b, int32_t id, void *data) {
    esp_hidh_event_data_t *param = data;
    if (id == ESP_HIDH_OPEN_EVENT) {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
        ESP_LOGI(BLE, BDASTR " connected (%s)",
                 BDA2STR(bda), esp_hidh_dev_name_get(param->open.dev));
        hiddev = param->open.dev;
    } else if (id == ESP_HIDH_BATTERY_EVENT) {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->battery.dev);
        ESP_LOGI(BLE, BDASTR " battery %d%%",
                 BDA2STR(bda), param->battery.level);
    } else if (id == ESP_HIDH_FEATURE_EVENT) {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->feature.dev);
        ESP_LOGI(BLE, BDASTR " %8s id %u size %d",
                 BDA2STR(bda), esp_hid_usage_str(param->feature.usage),
                 param->feature.report_id, param->feature.length);
        ESP_LOG_BUFFER_HEX(BLE, param->feature.data, param->feature.length);
    } else if (id == ESP_HIDH_CLOSE_EVENT) {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->close.dev);
        ESP_LOGI(BLE, BDASTR " closed (%s)",
                 BDA2STR(bda), esp_hidh_dev_name_get(param->close.dev));
        hiddev = NULL;
    }
    if (id != ESP_HIDH_INPUT_EVENT || !param->input.length) return;
    uint16_t len = param->input.length;
    int usage = param->input.usage, rid = param->input.report_id;
    if (rid == REPORT_ID_KEYBD || usage == ESP_HID_USAGE_KEYBOARD) {
        hid_keybd_report_t *kbd = (hid_keybd_report_t *)param->input.data;
        if (len < sizeof(*kbd)) return;
        hid_report_t report = { .id = rid, .keybd = *kbd };
        hid_report_send(HID_TARGET_SCN, &report);
        hid_handle_keybd(HID_TARGET_BLE, kbd, NULL);
    } else if (rid == REPORT_ID_MOUSE || usage == ESP_HID_USAGE_MOUSE) {
        hid_mouse_report_t *mse = (hid_mouse_report_t *)param->input.data;
        if (len < sizeof(*mse)) return;
        hid_report_t report = { .id = rid, .mouse = *mse };
        hid_report_send(HID_TARGET_SCN, &report);
        hid_handle_mouse(HID_TARGET_BLE, mse, NULL, NULL);
    } else if (rid == REPORT_ID_SCTRL && len == 1) {
        hid_report_sctrl(HID_TARGET_SCN, param->input.data[0]);
    } else if (rid == REPORT_ID_SDIAL && len == 2) {
        hid_report_sdial(HID_TARGET_SCN, param->input.data[0]);
    } else {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->input.dev);
        int offset = printf(BDASTR " %s ID %d ",
                            BDA2STR(bda), esp_hid_usage_str(usage), rid);
        if (offset > 0) hexdumpl(param->input.data, len, 80 - offset);
    }
}

static const esp_hidh_config_t conf = {
    .event_stack_size = 4096,
    .callback = ble_hidh_cb,
    .callback_arg = NULL,
};

esp_err_t ble_hidh_init(btmode_t prev) {
    if (hid_enabled) return ESP_OK;
    esp_err_t err = bt_common_init(ESP_BT_MODE_BLE, !ISBLE(prev));
    if (!err)
        err = esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler);
    if (!err) esp_hidh_init(&conf);
    hid_enabled = !err;
    return err;
}

esp_err_t ble_hidh_exit(btmode_t next) {
    if (!hid_enabled) return ESP_OK;
    esp_err_t err = esp_hidh_deinit();
    if (!err && !ISBLE(next)) err = bt_common_exit(true);
    hid_enabled = false;
    return err;
}

#else

esp_err_t ble_hidh_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ble_hidh_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_BLE_HID_HOST

void bthost_status(btmode_t mode) {
#ifdef CONFIG_BASE_BLE_HID_HOST
    if (mode == BLE_HIDH) {
        if (!hiddev) {
            puts("Not connected");
        } else {
            printf("Connected to " BDASTR " (%s)\n",
                   BDA2STR(esp_hidh_dev_bda_get(hiddev)),
                   esp_hidh_dev_name_get(hiddev));
        }
    }
#else
    return; NOTUSED(mode);
#endif
}

esp_err_t bthost_connect(
    esp_bd_addr_t bda, esp_bt_dev_type_t devtype, uint8_t remote_addr_type
) {
#ifdef CONFIG_BASE_BLE_HID_HOST
    if (hiddev || !hid_enabled) return ESP_ERR_INVALID_STATE;
    return esp_hidh_dev_open(
        bda, devtype == ESP_BT_DEVICE_TYPE_BLE
            ? ESP_HID_TRANSPORT_BLE : ESP_HID_TRANSPORT_BT, remote_addr_type
    ) ? ESP_OK : ESP_FAIL;
#else
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(bda); NOTUSED(transport); NOTUSED(remote_addr_type);
#endif
}

#endif // CONFIG_BASE_USE_BT
