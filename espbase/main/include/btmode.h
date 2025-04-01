/*
 * File: btmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/13 13:46:11
 */

#pragma once

#include "globals.h"
#include "hidtool.h"

#ifdef CONFIG_BASE_CAM_ATCAM
#   undef CONFIG_BASE_USE_BT
#endif

#if defined(CONFIG_BASE_BT_HID_DEVICE) && !defined(CONFIG_BT_HID_DEVICE_ENABLED)
#   warning "Run `idf.py menuconfig` -> Component config -> Bluetooth -> Bluedroid Options -> Classic BT HID -> Classic BT HID Device"
#   undef CONFIG_BASE_BT_HID_DEVICE
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_HIDD,
    BLE_HIDD,
    BLE_HIDH,
} btmode_t;

#define ISBT(m)     ( (m) == BT_HIDD )
#define ISBLE(m)    ( (m) == BLE_HIDD || (m) == BLE_HIDH )
#define ISSRV(m)    ( (m) == BT_HIDD || (m) == BLE_HIDD )
#define ISCLI(m)    ( (m) == BLE_HIDH )

esp_err_t btmode_switch(btmode_t mode, bool reboot_now);

void btmode_initialize();

void btmode_status();

// For server and client
esp_err_t btmode_scan(uint32_t timeout_ms);

// For server
esp_err_t btmode_config(bool connectable, bool discoverable);
esp_err_t btmode_battery(uint8_t pcent);

// For client
esp_err_t btmode_connect(const char *name, uint8_t *bda);

#ifdef CONFIG_BASE_USE_BT
bool hidb_send_report(const hid_report_t *);
#endif

#ifdef __cplusplus
}
#endif
