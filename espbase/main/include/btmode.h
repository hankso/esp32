/*
 * File: btmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/13 13:46:11
 */

#pragma once

#include "globals.h"
#include "hidtool.h"

#include "esp_bt.h"
#include "esp_bt_defs.h"

#define BDASTR      ESP_BD_ADDR_STR
#define BDA2STR     ESP_BD_ADDR_HEX
#define HAS_BT(m)   ( (m) & ESP_BT_MODE_CLASSIC_BT )
#define HAS_BLE(m)  ( (m) & ESP_BT_MODE_BLE )

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

typedef struct scan_rst {
    struct scan_rst *next;
    char name[64];
    int8_t rssi;
    esp_bd_addr_t addr;
    esp_bt_dev_type_t dev_type;
    union {
        struct {
            uint32_t cod;
            esp_bt_uuid_t uuid;
        } bt;
        struct {
            uint16_t gatts_uuid;
            uint16_t appearance;
            esp_ble_addr_type_t addr_type;
        } ble;
    };
} scan_rst_t;

esp_err_t btmode_scan(uint32_t timeout_ms);
scan_rst_t * btmode_find_device(const char *name, uint8_t *bda);

// For server
esp_err_t btmode_config(bool connectable, bool discoverable);
esp_err_t btmode_battery(uint8_t pcent);

// For client
esp_err_t btmode_connect(scan_rst_t *);

#ifdef CONFIG_BASE_USE_BT
bool hidb_send_report(const hid_report_t *);
#endif

#ifdef __cplusplus
}
#endif
