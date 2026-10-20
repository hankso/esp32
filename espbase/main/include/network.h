/*
 * File: network.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2020-03-21 13::33:32
 * Desc: Network drivers and WiFi helper functions need 200KB+ in firmware
 */

#pragma once

#include "globals.h"
#include "timesync.h"

#if defined(CONFIG_BASE_USE_WIFI) || defined(CONFIG_BASE_USE_ETH)
#   define CONFIG_BASE_USE_NET
#endif

#if defined(CONFIG_BASE_USE_WIFI) && defined(CONFIG_LWIP_IPV4_NAPT)
#   define WITH_NAPT    // ~2KB
#endif

#if __has_include("pcap.h")
#   define WITH_PCAP    // ~4KB
#else
#   warning "Run `idf.py add-dependency espressif/pcap`"
#endif

#if __has_include("mqtt_client.h")
#   define WITH_MQTT    // ~30KB
#else
#   warning "Run `idf.py add-dependency espressif/mqtt`"
#endif

#if defined(IDF_TARGET_V4) || __has_include("mdns.h")
#   define WITH_MDNS    // ~34KB
#else
#   warning "Run `idf.py add-dependency espressif/mdns`"
#endif

#if defined(IDF_TARGET_V4) || __has_include("iperf.h")
#   define WITH_IPERF   // ~ 5KB
#else
#   warning "Run `idf.py add-dependency espressif/iperf`"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void network_initialize();
esp_err_t network_command(const char *itf, const char *cmd,
                          const char *ssid, const char *pass,
                          const char *static_ipv4, uint16_t timeout_ms);
#define network_control(i, c, s) network_command((i), (c), (s), NULL, NULL, 0)

#ifdef CONFIG_BASE_USE_WIFI
esp_err_t wifi_ap_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_ap_stop();
esp_err_t wifi_ap_list_sta();

esp_err_t wifi_sta_start(const char *ssid, const char *pass, const char *ip);
esp_err_t wifi_sta_stop(bool down);
esp_err_t wifi_sta_scan(const char *ssid, uint8_t channel,
                        uint16_t timeout_ms, bool verbose);
esp_err_t wifi_sta_list_ap();
esp_err_t wifi_sta_wait(uint32_t timeout_ms);

typedef struct {
    uint8_t mac[6], lmk[16];
    int apifidx, channel, encrypt, powersave, longrange, broadcast; // -1 means auto
} now_param_t;

esp_err_t now_command(const char *cmd, now_param_t *param, const char *msg);

esp_err_t ftm_request(const char *ssid, uint8_t count);
esp_err_t ftm_respond(const char *cmd, int16_t offset_cm);
#endif

#if defined(CONFIG_BASE_USE_WIFI) || defined(CONFIG_BASE_USE_ETH)
esp_err_t pcap_command(const char *cmd, const char *itf, uint32_t npkt);

typedef struct {
    const void *msg;
    size_t len;
    uint8_t qos;
    bool retain;
} mqtt_pub_t;

esp_err_t mqtt_command(const char *cmd, const char *url,
                       const char *topic, const mqtt_pub_t *pub);
#define   mqtt_control(c) mqtt_command((c), NULL, NULL, NULL)

esp_err_t pmap_command(const char *cmd, const char *protocol,
                       uint16_t sport, const char *daddr, uint16_t dport);
#define   pmap_control(c) pmap_command((c), NULL, 0, NULL, 0)

esp_err_t mdns_command(const char *cmd, const char *hostname,
                       const char *service, const char *protocol,
                       uint16_t timeout_ms);
#define   mdns_control(c) mdns_command((c), NULL, NULL, NULL, 0)

esp_err_t sntp_command(const char *cmd, const char *host,
                       const char *mode, uint32_t interval_ms);
#define   sntp_control(c) sntp_command((c), NULL, NULL, 0)

esp_err_t ping_command(const char *host, uint16_t interval_ms,
                       uint16_t pktsize, uint16_t count, bool abort);
#define   ping_abort() ping_command(NULL, 0, 0, 0, true)

esp_err_t iperf_command(const char *host, uint16_t port,
                        uint16_t length, uint8_t interval_sec,
                        uint8_t timeout_sec, bool udp, bool abort);
#define   iperf_abort() iperf_command(NULL, 0, 0, 0, 0, false, true)

esp_err_t tsync_command(const char *host, const char *port,
                        uint32_t timeout_ms, bool abort);
#define   tsync_abort() tsync_command(NULL, NULL, 0, true)

esp_err_t hbeat_command(const char *cmd, const char *hurl,
                        const char *iurl, float hbtime, float intval);
#define   hbeat_control(c) hbeat_command((c), NULL, NULL, -1, -1)
#endif

#ifdef __cplusplus
}
#endif
