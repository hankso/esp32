/*
 * File: config.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 21:46:59
 */

#pragma once

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

// All values (number, boolean, text) are stored as strings
// `on`, `yes` are interpreted as True (see utils.c -> strbool)

typedef struct config_system {
    const char * DIR_DATA;  // Directory to storage font, image files etc.
    const char * DIR_DOCS;  // Directory to generated documentation
    const char * DIR_HTML;  // Directory to static webpage files
    const char * BTN_HIGH;  // Button active high
    const char * INT_EDGE;  // Select interrupt type
    const char * ADC_MULT;  // ADC multi-sampling to filter noise
    const char * USB_MODE;  // Select USB work mode
    const char * BT_MODE;   // Select BT work mode
    const char * BT_SCAN;   // Bluetooth discoverable
} config_sys_t;

typedef struct config_network {
    const char * STA_SSID;  // SSID of the AP to connect after startup
    const char * STA_PASS;  // Password of the AP to connect
    const char * STA_HOST;  // Static IP address (ignore DHCP)
    const char * AP_SSID;   // SSID of the AP to serve (hotspot name)
    const char * AP_PASS;   // Password of the AP to serve
    const char * AP_HOST;   // Gateway IP address
    const char * AP_CHAN;   // Channel of AP (1~14)
    const char * AP_NCON;   // Max number of stations of AP (1~253)
    const char * AP_NAPT;   // Enable WiFi NAT router (IDF_TARGET_V5)
    const char * AP_HIDE;   // Hide AP SSID (not shown on scan)
    const char * AP_AUTO;   // Switch to AP mode if STA connection failed
    const char * SC_AUTO;   // Enable SmartConfig if STA connection failed
} config_net_t;

typedef struct config_webserver {
    const char * WS_NAME;   // Username to auth websocket connection
    const char * WS_PASS;   // Password to auth websocket connection
    const char * HTTP_NAME; // Username to auth webserver (HTTP)
    const char * HTTP_PASS; // Password to auth webserver (HTTP)
    const char * AUTH_BASE; // Use basic HTTP authorization method (base64)
} config_web_t;

typedef struct config_application {
    const char * MDNS_RUN;  // Enable mDNS service
    const char * MDNS_HOST; // Register mDNS hostname
    const char * SNTP_RUN;  // Enable SNTP service
    const char * SNTP_HOST; // NTP server to sync time from
    const char * TSCN_MODE; // Select touchscreen mode
    const char * HID_MODE;  // Select gamepad layout
    const char * HID_HOST;  // UDP target IP address
    const char * OTA_AUTO;  // Enable auto updation checking
    const char * OTA_URL;   // URL to fetch firmware from
    const char * TIMEZONE;  // Set local timezone (see tzset(3) man)
    const char * PROMPT;    // Console promption string
} config_app_t;

// Informations are readonly after config_initialize
typedef struct config_information {
    const char * NAME;      // Program name
    const char * VER;       // Program version
    const char * UID;       // Unique serial number
} config_info_t;

// Global instance to access configuration
typedef struct {
    config_sys_t sys;
    config_net_t net;
    config_web_t web;
    config_app_t app;
    config_info_t info;
} config_t;

extern config_t Config;
enum { CFG_UPDATE };
ESP_EVENT_DECLARE_BASE(CFG_EVENT);

void config_initialize();
bool config_loads(const char *json);    // load Config from json
char * config_dumps();                  // dump Config into json
void config_stats();                    // print configurations

/* Get one config value by key or set one by key & value.
 *
 * Configuration key is "<category>.<domain>.<entry>", e.g.:
 *      Config.sys.DIR_DATA  -> "sys.dir.data"
 *      Config.app.MDNS_HOST -> "app.mdns.host"
 *
 * When a config entry is updated by config_set, the result is applied on
 * both Config (in RAM) and NVS Flash. It's not necessary to manually call
 * config_nvs_load / config_nvs_dump to sync between.
 */
const char * config_get(const char *key);
bool config_set(const char *key, const char *val);

/* NVS helper functions.
 * These are similar as Arduino-ESP32 library `Preference`.
 * But more lightweight and pure ESP-IDF.
 */
esp_err_t config_nvs_init();
esp_err_t config_nvs_open(const char *ns, bool ro); // open namespace in NVS
esp_err_t config_nvs_commit();          // must be called after config_nvs_open
esp_err_t config_nvs_close();           // close NVS with auto commit
bool config_nvs_remove(const char *key);// remove one entry in NVS
bool config_nvs_clear();                // remove all entries in NVS
bool config_nvs_load();                 // NVS Flash => Config RAM
bool config_nvs_dump();                 // Config RAM => NVS Flash
void config_nvs_stats();                // get NVS partition information
void config_nvs_list(bool all);         // print [all] entries in NVS

#ifdef __cplusplus
}
#endif
