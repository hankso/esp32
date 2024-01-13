/*
 * File: network.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-21 13:33:54
 */

#include "config.h"
#include "globals.h"
#include "network.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// For ping command
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"
// For iperf command
#include "iperf.h"

#ifndef CONFIG_WIFI_CHANNEL
#define CONFIG_WIFI_CHANNEL 1  // select from [1-13]
#define CONFIG_MAX_STA_CONN 4
#endif

#define UNCHANGED           -1

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAILURE_BIT    BIT1
#define WIFI_DISCONNECT_BIT BIT2
#define WIFI_SCAN_BLOCK_BIT BIT3
#define FTM_REPORT_BIT      BIT4
#define FTM_FAILURE_BIT     BIT5

#define HAS_STA(m)  ( m == WIFI_MODE_STA || m == WIFI_MODE_APSTA )
#define HAS_AP(m)   ( m == WIFI_MODE_AP || m == WIFI_MODE_APSTA )

static const char *TAG = "Network";

static EventGroupHandle_t evtgrp;

static esp_netif_t *if_sta, *if_ap;

static wifi_config_t config_ap = {
    .ap = {
        .channel = CONFIG_WIFI_CHANNEL,
        .max_connection = CONFIG_MAX_STA_CONN,
        .ftm_responder = true
    }
};

static wifi_config_t config_sta = {
    .sta = {
        .threshold.authmode = WIFI_AUTH_OPEN,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH
    }
};

static const char * wifi_authmode_str(wifi_auth_mode_t auth) {
    switch (auth) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    case WIFI_AUTH_WAPI_PSK:        return "WAPI";
    default: break;
    }
    return "unknown";
}

static const char * wifi_mode_str(wifi_mode_t mode) {
    switch (mode) {
    case WIFI_MODE_NULL:    return "NULL";
    case WIFI_MODE_STA:     return "STA";
    case WIFI_MODE_AP:      return "AP";
    case WIFI_MODE_APSTA:   return "AP+STA";
    default: break;
    }
    return "unknown";
}

static esp_err_t wifi_mode_switch(int sta, int ap, wifi_mode_t *mode) {
    wifi_mode_t origin, target;
    esp_err_t err = esp_wifi_get_mode(&origin);
    if (err) return err;
    target = origin;
    if (sta > 0) {
        target = HAS_AP(target) ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    } else if (!sta) {
        target = HAS_AP(target) ? WIFI_MODE_AP : WIFI_MODE_NULL;
    }
    if (ap > 0) {
        target = HAS_STA(target) ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    } else if (!ap) {
        target = HAS_STA(target) ? WIFI_MODE_STA : WIFI_MODE_NULL;
    }
    if (target != origin) {
        if (( err = esp_wifi_set_mode(target) ))
            return err;
        ESP_LOGI(TAG, "Switch mode from %s to %s",
                wifi_mode_str(origin), wifi_mode_str(target));
    }
    if (mode)
        *mode = target;
    return err;
}

static esp_err_t wifi_dhcp_switch(int sta, int ap) {
    esp_err_t err = ESP_ERR_INVALID_STATE;
    esp_netif_dhcp_status_t st_sta, st_ap;
    if (if_sta) {
        if (( err = esp_netif_dhcpc_get_status(if_sta, &st_sta) ))
            return err;
        if (sta > 0 && st_sta != ESP_NETIF_DHCP_STARTED) {
            return esp_netif_dhcpc_start(if_sta);
        } else if (!sta && st_sta != ESP_NETIF_DHCP_STOPPED) {
            return esp_netif_dhcpc_stop(if_sta);
        }
    }
    if (if_ap) {
        if (( err = esp_netif_dhcps_get_status(if_ap, &st_ap) ))
            return err;
        if (ap > 0 && st_ap != ESP_NETIF_DHCP_STARTED) {
            return esp_netif_dhcps_start(if_ap);
        } else if (!ap && st_ap != ESP_NETIF_DHCP_STOPPED) {
            return esp_netif_dhcps_stop(if_ap);
        }
    }
    return err;
}

static uint32_t wifi_local_ip(esp_netif_t *if_ptr) {
    esp_netif_ip_info_t ip = { 0 };
    if (!if_ptr) {
        wifi_mode_t mode = WIFI_MODE_NULL;
        wifi_mode_switch(UNCHANGED, UNCHANGED, &mode);
        if (HAS_STA(mode) && xEventGroupGetBits(evtgrp) & WIFI_CONNECTED_BIT) {
            if_ptr = if_sta;
        } else if (HAS_AP(mode)) {
            if_ptr = if_ap;
        } else {
            return 0;
        }
    }
    esp_netif_get_ip_info(if_ptr, &ip);
    return ip.ip.addr;
}

static void wifi_print_ipaddr(esp_netif_t * if_ptr) {
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(if_ptr, &ip);
    ESP_LOGI(TAG, "IP: " IPSTR ", GW: " IPSTR ", Mask: " IPSTR "\n",
             IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
}

static void wifi_print_apinfo(wifi_ap_record_t *aps, int num) {
    size_t maxlen = 10;
    LOOPN(i, num) {
        if (!aps[i].country.cc[0]) {
            aps[i].country.cc[0] = aps[i].country.cc[1] = ' ';
        }
#ifdef CONFIG_AUTO_ALIGN
        size_t len = strlen((char *)aps[i].ssid);
        if (len > maxlen) maxlen = len;
#else
        maxlen = 24;
#endif
    }
    printf("SSID%*s", maxlen - 4, "");
    puts(" MAC address       RSSI Mode WPS FTM Auth   CC Channel");
    LOOPN(i, num) {
        const char *ftm = "";
        if (aps[i].ftm_responder && aps[i].ftm_initiator) {
            ftm = "yes";
        } else if (aps[i].ftm_responder) {
            ftm = "REP";
        } else if (aps[i].ftm_initiator) {
            ftm = "REQ";
        }
        printf("%-*s " MACSTR " %4d %c%c%c%c %3s %3s %-6s %c%c %2d (%d-%d)\n",
            maxlen, (char *)aps[i].ssid, MAC2STR(aps[i].bssid), aps[i].rssi,
            aps[i].phy_11b ? 'b' : ' ', aps[i].phy_11g ? 'g' : ' ',
            aps[i].phy_11n ? 'n' : ' ', aps[i].phy_lr ? 'l' : 'h',
            aps[i].wps ? "yes" : "", ftm, wifi_authmode_str(aps[i].authmode),
            aps[i].country.cc[0], aps[i].country.cc[1], aps[i].primary,
            aps[i].country.schan, aps[i].country.nchan);
    }
}

static void wifi_print_aplist() {
    uint16_t nap = 0;
    wifi_ap_record_t *aps;
    esp_err_t err = esp_wifi_scan_get_ap_num(&nap);
    if (err) {
        ESP_LOGE(TAG, "STA scan failed: %s", esp_err_to_name(err));
    } else if (!nap) {
        ESP_LOGE(TAG, "STA no AP found");
    } else if (!( aps = malloc(nap * sizeof(wifi_ap_record_t)) )) {
        ESP_LOGE(TAG, "STA found %d AP. Failed to malloc buffer", nap);
    } else {
        if (( err = esp_wifi_scan_get_ap_records(&nap, aps) )) {
            ESP_LOGE(TAG, "STA get AP failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "STA found %d AP", nap);
            putchar('\n');
            wifi_print_apinfo(aps, nap);
        }
        free(aps);
    }
}

static esp_err_t wifi_find_ap(const char *ssid, uint8_t *bssid, wifi_ap_record_t *record) {
    esp_err_t err;
    wifi_ap_record_t *aps;
    wifi_scan_config_t scan_config = { .ssid = (uint8_t *)ssid };
    xEventGroupSetBits(evtgrp, WIFI_SCAN_BLOCK_BIT);
    uint16_t nap = 0, found = 0;
    if (
        ( err = esp_wifi_scan_start(&scan_config, true) ) ||
        ( err = esp_wifi_scan_get_ap_num(&nap) ) || !nap ||
        !( aps = malloc(nap * sizeof(wifi_ap_record_t)) )
    )
        return err ? err : ESP_ERR_NOT_FOUND;
    if (!( err = esp_wifi_scan_get_ap_records(&nap, aps) )) {
        LOOPN(i, nap) {
            if (
                (ssid && !strcmp((char *)aps[i].ssid, ssid)) ||
                (bssid && !memcmp(aps[i].bssid, bssid, sizeof(aps[i].bssid)))
            ) {
                *record = aps[i]; // copy whole structure
                found = true;
                break;
            }
        }
    }
    free(aps);
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t wifi_mode_check(wifi_interface_t interface) {
    wifi_mode_t mode;
    esp_err_t err = wifi_mode_switch(UNCHANGED, UNCHANGED, &mode);
    if (!err) {
        if (interface == WIFI_IF_AP && !HAS_AP(mode)) {
            puts("AP not enabled");
            err = ESP_ERR_INVALID_STATE;
        }
        if (interface == WIFI_IF_STA && !HAS_STA(mode)) {
            puts("STA not enabled");
            err = ESP_ERR_INVALID_STATE;
        }
    }
    return err;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    // For sys_evt stack overflow, try to uncomment next line:
    ESP_LOGD(TAG, "event stack %d", uxTaskGetStackHighWaterMark(NULL));
    static int retry = 0;
    if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            wifi_print_ipaddr(if_sta);
        } else if (id == IP_EVENT_AP_STAIPASSIGNED) {
            ip_event_ap_staipassigned_t *evt = data;
            ESP_LOGI(TAG, "AP client " IPSTR " assigned", IP2STR(&evt->ip));
        } else {
            ESP_LOGD(TAG, "Unhandled %s 0x%04X %p", base, id, data);
        }
    } else if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_START) {
            wifi_config_t cfg;
            esp_wifi_get_config(WIFI_IF_AP, &cfg);
            ESP_LOGI(TAG, "AP SSID %s, PASS %s, CH %d",
                cfg.ap.ssid, cfg.ap.password, cfg.ap.channel);
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *evt = data;
            ESP_LOGI(TAG, "AP client " MACSTR " join, AID=%d, Mesh=%d",
                    MAC2STR(evt->mac), evt->aid, evt->is_mesh_child);
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *evt = data;
            ESP_LOGI(TAG, "AP client " MACSTR " leave, AID=%d, Mesh=%d",
                    MAC2STR(evt->mac), evt->aid, evt->is_mesh_child);
        } else if (id == WIFI_EVENT_STA_CONNECTED) {
            xEventGroupSetBits(evtgrp, WIFI_CONNECTED_BIT);
            xEventGroupClearBits(evtgrp, WIFI_FAILURE_BIT);
            xEventGroupClearBits(evtgrp, WIFI_DISCONNECT_BIT);
            wifi_event_sta_connected_t *evt = data;
            ESP_LOGI(TAG, "STA connect `%s` success", evt->ssid);
            retry = 0;
            if (strbool(Config.net.AP_AUTO))
                wifi_ap_stop();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(evtgrp, WIFI_CONNECTED_BIT);
            wifi_event_sta_disconnected_t *evt = data;
            if (xEventGroupGetBits(evtgrp) & WIFI_DISCONNECT_BIT) {
                ESP_LOGI(TAG, "STA disconnect from `%s`", evt->ssid);
                xEventGroupClearBits(evtgrp, WIFI_DISCONNECT_BIT);
            } else if (evt->reason == WIFI_REASON_NO_AP_FOUND || retry > 2) {
                retry = 0;
                ESP_LOGW(TAG, "STA connect `%s` failed: 0x%02X", evt->ssid, evt->reason);
                xEventGroupSetBits(evtgrp, WIFI_FAILURE_BIT);
                if (strbool(Config.net.AP_AUTO))
                    wifi_ap_start(NULL, NULL, NULL);
            } else  {
                retry++;
                esp_wifi_connect();
                ESP_LOGI(TAG, "STA connect `%s` retry %d", evt->ssid, retry);
            }
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            if (xEventGroupGetBits(evtgrp) & WIFI_SCAN_BLOCK_BIT) {
                xEventGroupClearBits(evtgrp, WIFI_SCAN_BLOCK_BIT);
            } else {
                wifi_print_aplist();
            }
        } else {
            ESP_LOGD(TAG, "Unhandled %s 0x%04X %p", base, id, data);
        }
    } else {
        ESP_LOGD(TAG, "Unhandled %s 0x%04X %p", base, id, data);
    }
}

void network_initialize() {
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    if_ap = esp_netif_create_default_wifi_ap();
    if_sta = esp_netif_create_default_wifi_sta();
    evtgrp = xEventGroupCreate();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&init_config) );

    esp_event_base_t bases[2] = { WIFI_EVENT, IP_EVENT };
    LOOPN(i, LEN(bases)) {
        ESP_ERROR_CHECK( esp_event_handler_instance_register(
            bases[i], ESP_EVENT_ANY_ID, &event_handler, NULL, NULL
        ) );
    }
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    esp_err_t err = wifi_sta_start(0, 0, 0);
    if (!err) return;
    if (err != ESP_ERR_INVALID_ARG) {
        ESP_LOGE(TAG, "Failed to start STA: %s", esp_err_to_name(err));
    } else if (strbool(Config.net.AP_AUTO) && ( err = wifi_ap_start(0, 0, 0) )) {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(err));
    }
}

esp_err_t wifi_sta_start(const char *ssid, const char *pass, const char *ip) {
    // Arguments validation
    if (!ssid) {
        if (!strlen(Config.net.STA_SSID))
            return ESP_ERR_INVALID_ARG;
        ssid = Config.net.STA_SSID;
    }
    if (!pass)
        pass = Config.net.STA_PASS;
    if (!ip && strlen(Config.net.STA_HOST))
        ip = Config.net.STA_HOST;

    // WiFi mode validation
    esp_err_t err = wifi_mode_switch(true, UNCHANGED, NULL);
    if (err) return err;
    if (xEventGroupGetBits(evtgrp) & WIFI_CONNECTED_BIT) {
        wifi_ap_record_t record;
        err = esp_wifi_sta_get_ap_info(&record);
        if (err == ESP_OK && !strcmp((char *)record.ssid, ssid))
            return err;                         // already connected to this AP
        if (err != ESP_ERR_WIFI_NOT_CONNECT)
            wifi_sta_stop();                    // disconnect from current AP
    }

    // Configure static IP address
    if (ip && !( err = wifi_dhcp_switch(false, UNCHANGED) )) {
        esp_netif_ip_info_t ip_sta = {
            .ip.addr = ipaddr_addr(ip),
            .gw.addr = (ipaddr_addr(ip) & ~0xFF) | 0x01,
            .netmask.addr = ipaddr_addr("255.255.255.0")
        };
        if (( err = esp_netif_set_ip_info(if_sta, &ip_sta) )) {
            ESP_LOGE(TAG, "STA static IP failed: %s", esp_err_to_name(err));
            wifi_dhcp_switch(true, UNCHANGED);
        }
    } else if (!ip && ( err = wifi_dhcp_switch(true, UNCHANGED) )) {
        return err;
    }

    // Connect to the specified AP
    // Note: Do NOT use strcpy/strncpy because we have to overwrite old values
    wifi_sta_config_t *sta = &config_sta.sta;
    snprintf((char *)sta->ssid, sizeof(sta->ssid), "%s", ssid);
    if (strlen(pass)) {
        snprintf((char *)sta->password, sizeof(sta->password), "%s", pass);
    } else {
        sta->password[0] = 0;
    }
    if (( err = esp_wifi_set_config(WIFI_IF_STA, &config_sta) ))
        return err;
    return esp_wifi_connect();
}

esp_err_t wifi_sta_stop() {
    xEventGroupSetBits(evtgrp, WIFI_DISCONNECT_BIT);
    return esp_wifi_disconnect();
}

esp_err_t wifi_sta_scan(const char * ssid, uint8_t channel, uint16_t timeout_ms) {
    esp_err_t err = wifi_mode_check(WIFI_IF_STA);
    if (err == ESP_ERR_INVALID_STATE) return ESP_OK;
    if (err || ( err = esp_wifi_scan_stop() )) return err;
    esp_wifi_clear_ap_list();
    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t *)ssid,
        .channel = channel,
        .show_hidden = true
    };
    if (timeout_ms < 1300) {
        xEventGroupClearBits(evtgrp, WIFI_SCAN_BLOCK_BIT);
        return esp_wifi_scan_start(&scan_config, false);
    }
    scan_config.scan_time.active.min = timeout_ms / 13;
    scan_config.scan_time.active.max = timeout_ms / 16;
    xEventGroupSetBits(evtgrp, WIFI_SCAN_BLOCK_BIT);
    if (!( err = esp_wifi_scan_start(&scan_config, true) ))
        wifi_print_aplist();
    return err;
}

esp_err_t wifi_sta_wait(uint16_t timeout_ms) {
    EventBits_t wait = WIFI_CONNECTED_BIT | WIFI_DISCONNECT_BIT | WIFI_FAILURE_BIT;
    EventBits_t bits = xEventGroupWaitBits(
        evtgrp, wait, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    if (bits & WIFI_CONNECTED_BIT)
        return ESP_OK;
    if (bits & WIFI_DISCONNECT_BIT) {
        xEventGroupClearBits(evtgrp, WIFI_DISCONNECT_BIT);
        esp_wifi_connect();
        return wifi_sta_wait(timeout_ms);
    }
    if (bits & WIFI_FAILURE_BIT)
        return ESP_FAIL;
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_ap_start(const char *ssid, const char *pass, const char *ip) {
    if (!ssid) {
        if (!strlen(Config.net.AP_SSID))
            return ESP_ERR_INVALID_ARG;
        ssid = Config.net.AP_SSID;
    }
    if (!pass)
        pass = Config.net.AP_PASS;
    if (!ip && strlen(Config.net.AP_HOST))
        ip = Config.net.AP_HOST;

    esp_err_t err = wifi_mode_switch(UNCHANGED, true, NULL);
    if (err) return err;

    if (ip && !( err = wifi_dhcp_switch(UNCHANGED, false) )) {
        esp_netif_ip_info_t ip_ap = {
            .ip.addr = ipaddr_addr(ip),
            .gw.addr = ipaddr_addr(ip),
            .netmask.addr = ipaddr_addr("255.255.255.0")
        };
        if (( err = esp_netif_set_ip_info(if_ap, &ip_ap) ))
            ESP_LOGE(TAG, "AP static IP failed: %s", esp_err_to_name(err));
        wifi_dhcp_switch(UNCHANGED, true);
    }

    wifi_ap_config_t *ap = &config_ap.ap;
    if (strlen(Config.info.UID)) {
        snprintf((char *)ap->ssid, sizeof(ap->ssid), "%s-%s", ssid, Config.info.UID);
    } else {
        snprintf((char *)ap->ssid, sizeof(ap->ssid), "%s", ssid);
    }
    ap->ssid_len = strlen((char *)ap->ssid);
    if (!pass || strlen(pass) < 8) {
        ap->authmode = WIFI_AUTH_OPEN;
        ap->password[0] = 0;
    } else {
        ap->authmode = WIFI_AUTH_WPA_WPA2_PSK;
        snprintf((char *)ap->password, sizeof(ap->password), "%s", pass);
    }
    return esp_wifi_set_config(WIFI_IF_AP, &config_ap);
}

esp_err_t wifi_ap_stop() { return wifi_mode_switch(UNCHANGED, false, NULL); }

esp_err_t wifi_sta_list_ap() {
    esp_err_t err = wifi_mode_check(WIFI_IF_STA);
    if (err == ESP_ERR_INVALID_STATE) return ESP_OK;
    if (err) return err;
    EventBits_t bits = xEventGroupGetBits(evtgrp);
    if (config_sta.sta.ssid && strlen((char *)config_sta.sta.ssid)) {
        printf("STA SSID: `%s`, Status: ", (char *)config_sta.sta.ssid);
    } else {
        printf("STA Status: ");
    }
    if (bits & WIFI_DISCONNECT_BIT) {
        puts("disconnected");
    } else if (bits & WIFI_FAILURE_BIT) {
        puts("failed");
    } else if (bits & WIFI_CONNECTED_BIT) {
        puts("connected");
        wifi_print_ipaddr(if_sta);
        wifi_ap_record_t info;
        if (( err = esp_wifi_sta_get_ap_info(&info) ))
            return err;
        putchar('\n');
        wifi_print_apinfo(&info, 1);
    } else {
        puts("not initialized");
    }
    return ESP_OK;
}

esp_err_t wifi_ap_list_sta() {
    esp_err_t err = wifi_mode_check(WIFI_IF_AP);
    if (err == ESP_ERR_INVALID_STATE) return ESP_OK;
    if (err) return err;
    printf("AP SSID %s CH %d\n", config_ap.ap.ssid, config_ap.ap.channel);
    wifi_print_ipaddr(if_ap);

    uint16_t aid;
    wifi_sta_list_t wifi_sta_list;
    esp_netif_sta_list_t netif_sta_list;
    if (( err = esp_wifi_ap_get_sta_list(&wifi_sta_list) ) ||
        ( err = esp_netif_get_sta_list(&wifi_sta_list, &netif_sta_list) ))
    {
        printf("Could not get sta list: %s\n", esp_err_to_name(err));
        return err;
    }
    if (!wifi_sta_list.num) {
        printf("No connected stations\n");
        return err;
    }
    printf("\nAID  IP address       MAC address       RSSI Mode Mesh\n");
    LOOPN(i, wifi_sta_list.num) {
        wifi_sta_info_t *hw = wifi_sta_list.sta + i;
        esp_netif_sta_info_t *sw = netif_sta_list.sta + i;
        if (( err = esp_wifi_ap_get_sta_aid(hw->mac, &aid) )) {
            ESP_LOGD(TAG, "Get STA AID failed: %s", esp_err_to_name(err));
            continue;
        }
        printf("%04X %-16s " MACSTR " %4d %c%c%c%c %s\n",
            aid, inet_ntoa(sw->ip), MAC2STR(sw->mac), hw->rssi,
            hw->phy_11b ? 'b' : ' ', hw->phy_11g ? 'g' : ' ',
            hw->phy_11n ? 'n' : ' ', hw->phy_lr ? 'l' : 'h',
            hw->is_mesh_child ? "true" : "false");
    }
    return err;
}

esp_err_t iperf_command(const char *host, uint16_t port, uint16_t length,
                        uint32_t interval_sec, uint32_t timeout_sec,
                        bool abort, bool udp)
{
    if (abort) return iperf_stop();

    uint32_t flag = host ? IPERF_FLAG_CLIENT : IPERF_FLAG_SERVER;
    uint32_t src_ip = wifi_local_ip(NULL);
    uint32_t dst_ip = ipaddr_addr(host ? host : "");
    if (!src_ip) return ESP_ERR_INVALID_STATE;
    if (host && dst_ip == IPADDR_NONE) return ESP_ERR_INVALID_ARG;

    iperf_cfg_t config = {
        .flag = flag | (udp ? IPERF_FLAG_UDP : IPERF_FLAG_TCP),
        .destination_ip4 = host ? dst_ip : 0,
        .source_ip4 = src_ip,
        .type = IPERF_IP_TYPE_IPV4,
        .dport = (port && host) ? port : IPERF_DEFAULT_PORT,
        .sport = (port && !host) ? port : IPERF_DEFAULT_PORT,
        .interval = interval_sec ? interval_sec : IPERF_DEFAULT_INTERVAL,
        .time = timeout_sec ? timeout_sec : IPERF_DEFAULT_TIME,
        .len_send_buf = length,
        .bw_lim = IPERF_DEFAULT_NO_BW_LIMIT
    };
    if (config.time < config.interval)
        config.time = config.interval;
    char sip[IP4ADDR_STRLEN_MAX], dip[IP4ADDR_STRLEN_MAX];
    inet_ntoa_r(config.source_ip4, sip, IP4ADDR_STRLEN_MAX);
    inet_ntoa_r(config.destination_ip4, dip, IP4ADDR_STRLEN_MAX);
    ESP_LOGI(TAG, "mode=%s-%s sip=%s:%d, dip=%s:%d, interval=%d, time=%d",
            udp ? "udp" : "tcp", host ? "client" : "server",
            sip, config.sport, dip, config.dport, config.interval, config.time);
    return iperf_start(&config);
}

#define GET_PING_PROF(name, var) \
    esp_ping_get_profile(hdl, ESP_PING_PROF_ ## name, &(var), sizeof(var))

static void ping_command_success(esp_ping_handle_t hdl, void *args) {
    uint8_t ttl;
    uint16_t seqno;
    uint32_t dtms, size;
    ip_addr_t target;
    GET_PING_PROF(TTL, ttl);
    GET_PING_PROF(SIZE, size);
    GET_PING_PROF(SEQNO, seqno);
    GET_PING_PROF(TIMEGAP, dtms);
    GET_PING_PROF(IPADDR, target);
    if (seqno == 1) putchar('\n');
    printf("From %s: icmp_seq=%d bytes=%d time=%dms ttl=%d\n",
           ipaddr_ntoa((ip_addr_t *)&target), seqno, size, dtms, ttl);
}

static void ping_command_timeout(esp_ping_handle_t hdl, void *args) {
    uint16_t seqno;
    ip_addr_t target;
    GET_PING_PROF(SEQNO, seqno);
    GET_PING_PROF(IPADDR, target);
    const char *addr = ipaddr_ntoa((ip_addr_t *)&target);
    if (seqno == 1) putchar('\n');
    printf("From %s: icmp_seq=%d timeout\n", addr, seqno);
}

static void ping_command_end(esp_ping_handle_t hdl, void *args) {
    ip_addr_t target;
    const char *addr;
    uint32_t sent, recv, dtms;
    GET_PING_PROF(REPLY, recv);
    GET_PING_PROF(REQUEST, sent);
    GET_PING_PROF(DURATION, dtms);
    GET_PING_PROF(IPADDR, target);
    if (IP_IS_V4(&target)) {
        addr = inet_ntoa(*ip_2_ip4(&target));
    } else {
        addr = inet6_ntoa(*ip_2_ip6(&target));
    }
    printf("Ping %s: %d sent, %d recv, %d lost (%d%%) in %dms\n",
           addr, sent, recv, sent - recv, 100 * (sent - recv) / sent, dtms);
    esp_ping_delete_session(hdl);
}

esp_err_t ping_command(const char *host, uint16_t timeout_ms,
                       uint16_t data_size, uint16_t count)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;
    struct sockaddr_in6 sock_addr6;
    ip_addr_t target_addr = { 0 };

    if (inet_pton(AF_INET6, host, &sock_addr6.sin6_addr) == 1) {
        ipaddr_aton(host, &target_addr); // IPv6 string to address
    } else {
        struct addrinfo hint, *res = NULL;
        if (getaddrinfo(host, NULL, &hint, &res)) {
            printf("Invalid host to ping: %s\n", host);
            return err;
        }
        if (res->ai_family == AF_INET) {
            struct sockaddr_in *addr4 = (struct sockaddr_in *)res->ai_addr;
            inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4->sin_addr);
        } else {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)res->ai_addr;
            inet6_addr_to_ip6addr(ip_2_ip6(&target_addr), &addr6->sin6_addr);
        }
        freeaddrinfo(res);
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    if (timeout_ms) config.timeout_ms = timeout_ms;
    if (data_size)  config.data_size = data_size;
    if (count)      config.count = count;
    config.target_addr = target_addr;

    static esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_command_success,
        .on_ping_timeout = ping_command_timeout,
        .on_ping_end = ping_command_end,
        .cb_args = NULL
    };
    esp_ping_handle_t hdl;
    if (( err = esp_ping_new_session(&config, &cbs, &hdl) ))
        return err;
    return esp_ping_start(hdl);
}

esp_err_t ftm_responder(const char *ctrl, int16_t *offset_cm) {
    if (offset_cm && !esp_wifi_ftm_resp_set_offset(*offset_cm)) {
        ESP_LOGI(TAG, "AP set FTM responder offset to %dcm", *offset_cm);
    }
    esp_err_t err = wifi_mode_check(WIFI_IF_AP);
    if (err || !ctrl || strbool(ctrl) == config_ap.ap.ftm_responder)
        return err;
    config_ap.ap.ftm_responder = !config_ap.ap.ftm_responder;
    if (!( err = esp_wifi_set_config(WIFI_IF_AP, &config_ap) )) {
        ESP_LOGI(TAG, "AP set FTM responder to %s",
                config_ap.ap.ftm_responder ? "ON" : "OFF");
    }
    return err;
}

esp_err_t ftm_initiator(const char *ssid, uint16_t timeout_ms, uint8_t *count) {
    esp_err_t err = wifi_mode_check(WIFI_IF_STA);
    if (err) return err;

    wifi_ap_record_t record;
    if (ssid) {
        if (( err = wifi_find_ap(ssid, NULL, &record) ))
            return err;
    } else if (xEventGroupGetBits(evtgrp) & WIFI_CONNECTED_BIT) {
        if (( err = esp_wifi_sta_get_ap_info(&record) ))
            return err;
    } else {
        ESP_LOGE(TAG, "STA disconnected. FTM need the SSID of the AP");
        return ESP_ERR_INVALID_ARG;
    }
    if (!record.ftm_responder) {
        ESP_LOGE(TAG, "STA FTM not supported by `%s`", (char *)record.ssid);
        return ESP_ERR_INVALID_ARG;
    }
    wifi_ftm_initiator_cfg_t config = {
        .channel = record.primary,
        .frm_count = 32,
        .burst_period = 2   // 200ms
    };
    memcpy(config.resp_mac, record.bssid, sizeof(record.bssid));
    if (count && (*count % 8 == 0) && (*count <= 32 || *count == 64))
        config.frm_count = *count;
    ESP_LOGI(TAG, "STA FTM initiator " MACSTR " channel=%d count=%d period=%dms",
            MAC2STR(config.resp_mac), config.channel,
            config.frm_count, config.burst_period * 100);
    if (( err = esp_wifi_ftm_initiate_session(&config) ) || !timeout_ms)
        return err;
    EventBits_t want = FTM_REPORT_BIT | FTM_FAILURE_BIT;
    EventBits_t bits = xEventGroupWaitBits(
        evtgrp, want, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    if (bits & FTM_REPORT_BIT)
        return ESP_OK;
    if (bits & FTM_FAILURE_BIT)
        return ESP_FAIL;
    return ESP_ERR_TIMEOUT;
}
