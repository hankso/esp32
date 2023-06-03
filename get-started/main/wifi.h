/*
 * File: wifi.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2020-03-21 13::33:32
 *
 * WiFi drivers and STA/AP helper functions occupy about 217KB in firmware.
 *
 * After startup, esp32 firstly try to connect to an Acces Point. On connection
 * failed (on available access point or password mismatch), esp32 will turn
 * into STA_AP mode and establish a hotspot with AP_SSID & AP_PASS. Users can
 * connect to this hotspot and visit http://{AP_HOST}/ap/index.html to list all
 * scanned Access Points, select one and connect to it by setting config value
 * STA_SSID & STA_PASS.
 */

#ifndef _WIFI_H_
#define _WIFI_H_

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

void wifi_initialize();

esp_err_t wifi_ap_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_ap_stop();
esp_err_t wifi_ap_list_sta();

esp_err_t wifi_sta_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_sta_stop();
esp_err_t wifi_sta_scan(const char *ssid, uint8_t channel, uint16_t timeout_ms);
esp_err_t wifi_sta_wait(uint16_t timeout_ms);
esp_err_t wifi_sta_list_ap();

#ifdef __cplusplus
}
#endif

#endif // _WIFI_H_
