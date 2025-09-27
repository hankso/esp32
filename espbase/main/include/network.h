/*
 * File: network.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2020-03-21 13::33:32
 * Desc: Network drivers and WiFi helper functions need 200KB+ in firmware
 */

#pragma once

#include "globals.h"

#if defined(CONFIG_BASE_USE_WIFI) || defined(CONFIG_BASE_USE_ETH)
#   define CONFIG_BASE_USE_NET
#endif

#if __has_include("pcap.h")
#   define WITH_PCAP
#else
#   warning "Run `idf.py add-dependency pcap`"
#endif

#if defined(IDF_TARGET_V4) || __has_include("mdns.h")
#   define WITH_MDNS
#else
#   warning "Run `idf.py add-dependency mdns`"
#endif

#if defined(IDF_TARGET_V4) || __has_include("iperf.h")
#   define WITH_IPERF
#else
#   warning "Run `idf.py add-dependency iperf`"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void network_initialize();
esp_err_t network_parse_host(const char *host, void *ipaddr);
esp_err_t network_parse_addr(const char *host, uint16_t port, void *sockaddr);
esp_err_t network_command(const char *itf, const char *cmd,
                          const char *ssid, const char *pass,
                          const char *host, uint16_t timeout_ms);

#ifdef CONFIG_BASE_USE_WIFI
esp_err_t wifi_ap_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_ap_stop();
esp_err_t wifi_ap_list_sta();

esp_err_t wifi_sta_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_sta_stop();
esp_err_t wifi_sta_scan(const char *ssid, uint8_t channel,
                        uint16_t timeout_ms, bool verbose);
esp_err_t wifi_sta_wait(uint16_t timeout_ms);
esp_err_t wifi_sta_list_ap();

esp_err_t ftm_request(const char *ssid, uint8_t count);
esp_err_t ftm_respond(const char *ctrl, int16_t offset_cm);
#endif

#if defined(CONFIG_BASE_USE_WIFI) || defined(CONFIG_BASE_USE_ETH)
esp_err_t pcap_command(const char *ctrl, const char *itf, uint32_t npkt);

esp_err_t mdns_command(const char *ctrl, const char *hostname,
                       const char *service, const char *protocol,
                       uint16_t timeout_ms);
#define   mdns_control(c) mdns_command((c), NULL, NULL, NULL, 0)

esp_err_t sntp_command(const char *ctrl, const char *host,
                       const char *mode, uint32_t interval_ms);
#define   sntp_control(c) sntp_command((c), NULL, NULL, 0)

esp_err_t ping_command(const char *host, uint16_t interval_ms,
                       uint16_t data_size, uint16_t count, bool abort);
#define   ping_abort() ping_command(NULL, 0, 0, 0, true)

esp_err_t iperf_command(const char *host, uint16_t port,
                        uint16_t length, uint8_t interval_sec,
                        uint8_t timeout_sec, bool udp, bool abort);
#define   iperf_abort() iperf_command(NULL, 0, 0, 0, 0, false, true)

esp_err_t tsync_command(const char *host, uint16_t port,
                        uint32_t timeout_ms, bool abort);
#define   tsync_abort() tsync_command(NULL, 0, 0, true)

esp_err_t hbeat_command(const char *ctrl, const char *hurl,
                        const char *iurl, float hbtime, float intval);
#define   hbeat_control(c) hbeat_command((c), NULL, NULL, -1, -1)
#endif

#ifdef __cplusplus
}
#endif
