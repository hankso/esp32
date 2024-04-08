/*
 * File: config.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 21:46:59
 *
 * See more in comment strings
 */

#pragma once

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct config_network_t {
    const char * STA_SSID;  // SSID of the AP to connect after startup
    const char * STA_PASS;  // Password of the AP to connect
    const char * STA_HOST;  // Static IP address (ignore DHCP)
    const char * AP_SSID;   // SSID of the AP to serve (hotspot name)
    const char * AP_PASS;   // Password of the AP to serve
    const char * AP_HOST;   // IP address of Gateway
    const char * AP_AUTO;   // Switch to AP mode if STA connection failed
    const char * AP_HIDE;   // Hide AP SSID (not shown on scan)
} config_net_t;

typedef struct config_webserver_t {
    const char * WS_NAME;   // Username to auth websocket connection
    const char * WS_PASS;   // Password to auth websocket connection
    const char * HTTP_NAME; // Username to auth webserver (HTTP)
    const char * HTTP_PASS; // Password to auth webserver (HTTP)
    const char * DIR_DATA;  // Directory to storage user uploaded files
    const char * DIR_DOCS;  // Directory to generated documentation
    const char * DIR_ROOT;  // Directory to static webpage files
} config_web_t;

// For easier managing, Boolean values are stored as "0" or "1" (string)
typedef struct config_application_t {
    const char * MDNS_RUN;  // Enable mDNS service
    const char * MDNS_HOST; // Register mDNS hostname
    const char * SNTP_RUN;  // Enable SNTP service
    const char * SNTP_HOST; // NTP server to sync time from
    const char * OTA_RUN;   // Enable auto updation checking
    const char * OTA_URL;   // URL to fetch firmware from
    const char * USB_MODE;  // Select USB work mode
    const char * TIMEZONE;  // Set local timezone (see tzset(3) man)
    const char * PROMPT;    // Console promption string
} config_app_t;

// Information is readonly after initialization
typedef struct config_information_t {
    const char * NAME;      // Program name
    const char * VER;       // Program version
    const char * UID;       // Unique serial number
} config_info_t;

// Global instance to access configuration
typedef struct {
    config_net_t net;
    config_web_t web;
    config_app_t app;
    config_info_t info;
} config_t;

extern config_t Config;

void config_initialize();
bool config_loads(const char *);        // load Config from json
char * config_dumps();                  // dump Config into json
void config_list();                     // list all configurations

/* Get one config value by key or set one by key & value. When you update an
 * entry, the result is applied on both Config and NVS flash. You don't need
 * to call config_nvs_load/config_nvs_dump to sync between.
 */
const char * config_get(const char *);
bool config_set(const char *, const char *);

/* NVS helper functions.
 * These are similar as Arduino-ESP32 library `Preference`.
 * But more lightweight and without dependency on Arduino.
 */
esp_err_t config_nvs_init();
esp_err_t config_nvs_open(const char *, bool ro); // open namespace in NVS
esp_err_t config_nvs_commit();          // must be called after config_nvs_open
esp_err_t config_nvs_close();           // close NVS with auto commit
bool config_nvs_remove(const char *);   // remove one entry in NVS
bool config_nvs_clear();                // remove all entries in NVS
bool config_nvs_load();                 // load Config from NVS partition
bool config_nvs_dump();                 // save Config to NVS partition
void config_nvs_stats();                // get NVS partition detail
void config_nvs_list(bool);             // list [all] entries in NVS

#ifdef __cplusplus
}
#endif
