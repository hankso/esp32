/*
 * File: network.h
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

#pragma once

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

void network_initialize();

esp_err_t wifi_ap_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_ap_stop();
esp_err_t wifi_ap_list_sta();

esp_err_t wifi_sta_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_sta_stop();
esp_err_t wifi_sta_scan(const char *ssid, uint8_t channel,
                        uint16_t timeout_ms, bool verbose);
esp_err_t wifi_sta_wait(uint16_t timeout_ms);
esp_err_t wifi_sta_list_ap();

esp_err_t wifi_parse_addr(const char *host, void *dst);

esp_err_t ftm_request(const char *ssid, uint8_t count);
esp_err_t ftm_respond(const char *ctrl, int16_t offset_cm);

esp_err_t mdns_command(const char *ctrl, const char *hostname,
                       const char *service, const char *proto,
                       uint16_t timeout_ms);
#define   mdns_control(ctrl) mdns_command((ctrl), NULL, NULL, NULL, 0)

esp_err_t sntp_command(const char *ctrl, const char *host,
                       const char *mode, uint32_t interval_ms);
#define   sntp_control(ctrl) sntp_command((ctrl), NULL, NULL, 0)

esp_err_t ping_command(const char *host, uint16_t interval_ms,
                       uint16_t data_size, uint16_t count, bool abort);
#define   ping_abort() ping_command(NULL, 0, 0, 0, true)

esp_err_t iperf_command(const char *host, uint16_t port,
                        uint16_t length, uint8_t interval_sec,
                        uint8_t timeout_sec, bool udp, bool abort);
#define   iperf_abort() iperf_command(NULL, 0, 0, 0, 0, false, true)

esp_err_t timesync_command(const char *host, uint16_t port,
                           uint32_t timeout_ms, bool abort);
#define   timesync_abort() timesync_command(NULL, 0, 0, true)

#ifdef __cplusplus
}
#endif
