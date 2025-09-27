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
// `1`, `on`, `yes` are interpreted as True (see utils.c -> strtob)

typedef struct {
    const char * TIMEZONE;  // Set local timezone (see man tzset(3))
    const char * PROMPT;    // Console promption string
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

typedef struct {
    const char * ETH_HOST;  // Static IP address of Ethernet
    const char * ETH_GATE;  // Gateway IP address of Ethernet
    const char * STA_SSID;  // SSID of the AP to connect after startup
    const char * STA_PASS;  // Password of the AP to connect
    const char * STA_HOST;  // Static IP address of WiFi station
    const char * STA_GATE;  // Gateway IP address of WiFi station
    const char * AP_SSID;   // SSID of the AP to serve (hotspot name)
    const char * AP_PASS;   // Password of the AP to serve
    const char * AP_HOST;   // Gateway IP address
    const char * AP_CHAN;   // Channel of AP (1~14)
    const char * AP_NCON;   // Max number of stations of AP (1~253)
    const char * AP_NAPT;   // Enable WiFi NAT router (IDF V5+)
    const char * AP_HIDE;   // Hide AP SSID (not shown on scan)
    const char * AP_AUTO;   // Switch to AP mode if STA connection failed
    const char * SC_AUTO;   // Enable SmartConfig if STA connection failed
} config_net_t;

typedef struct {
    const char * WS_NAME;   // Username to auth websocket connection
    const char * WS_PASS;   // Password to auth websocket connection
    const char * HTTP_NAME; // Username to auth webserver (HTTP)
    const char * HTTP_PASS; // Password to auth webserver (HTTP)
    const char * AUTH_BASE; // Whether to use basic/digest HTTP authorization
} config_web_t;

typedef struct {
    const char * MDNS_RUN;  // Enable mDNS service
    const char * MDNS_HOST; // Register mDNS hostname
    const char * SNTP_RUN;  // Enable SNTP service
    const char * SNTP_HOST; // NTP server to sync time from
    const char * TSCN_MODE; // Select touchscreen mode
    const char * HID_MODE;  // Select gamepad layout
    const char * HID_HOST;  // UDP target IP address
    const char * HBT_AUTO;  // Auto start heartbeat task
    const char * HBT_URL;   // URL to post heartbeat info
    const char * OTA_AUTO;  // Enable auto updation checking
    const char * OTA_URL;   // URL to fetch firmware from
} config_app_t;

// Informations are readonly after config_initialize
typedef struct {
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
esp_err_t config_loads(const char *);   // load Config from json
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
esp_err_t config_set(const char *key, const char *val);

/* NVS helper functions (similar like Arduino-ESP32 library `Preference`) */
esp_err_t config_nvs_open(void **ptr, const char *ns, bool ro);
int config_nvs_read(void *hdl, const char *key, void *buf, size_t size);
int config_nvs_write(void *hdl, const char *key, const void *val, size_t len);
esp_err_t config_nvs_delete(void *hdl, const char *key); // remove or erase all
esp_err_t config_nvs_close(void **ptr); // close NVS with auto commit
esp_err_t config_nvs_load();            // NVS Flash => RAM Config
esp_err_t config_nvs_dump();            // RAM Config => NVS Flash
void config_nvs_list(bool all);         // print [all] entries in NVS

#ifdef __cplusplus
}
#endif
