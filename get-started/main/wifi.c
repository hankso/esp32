/*
 * File: wifi.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-21 13:33:54
 */

#include "globals.h"
#include "config.h"
#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"

#ifndef CONFIG_WIFI_CHANNEL
#define CONFIG_WIFI_CHANNEL 1  // select from [1-13]
#define CONFIG_MAX_STA_CONN 4
#endif

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAILED_BIT     BIT1
#define WIFI_DISCONNECT_BIT BIT2
#define UNCHANGED           -1

#define HAS_STA(m)  ( m == WIFI_MODE_STA || m == WIFI_MODE_APSTA )
#define HAS_AP(m)   ( m == WIFI_MODE_AP || m == WIFI_MODE_APSTA )

static const char *TAG = "Wifi";

static EventGroupHandle_t evtgrp;

static esp_netif_t *if_sta, *if_ap;

static wifi_config_t config_ap = {
    .ap = {
        .channel = CONFIG_WIFI_CHANNEL,
        .max_connection = CONFIG_MAX_STA_CONN
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

static void wifi_print_ipaddr(esp_netif_t * if_ptr) {
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(if_ptr, &ip);
    printf("IP: " IPSTR ", GW: " IPSTR ", Mask: " IPSTR "\n",
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
    puts(" MAC address       RSSI Mode Auth   CC Channel");
    LOOPN(i, num) {
        printf("%-*s " MACSTR " %4d %c%c%c%c %-6s %c%c %2d (%d-%d)\n",
            maxlen, (char *)aps[i].ssid, MAC2STR(aps[i].bssid), aps[i].rssi,
            aps[i].phy_11b ? 'b' : ' ', aps[i].phy_11g ? 'g' : ' ',
            aps[i].phy_11n ? 'n' : ' ', aps[i].phy_lr ? 'l' : 'h',
            wifi_authmode_str(aps[i].authmode),
            aps[i].country.cc[0], aps[i].country.cc[1], aps[i].primary,
            aps[i].country.schan, aps[i].country.nchan);
    }
}

static esp_err_t wifi_mode_check(wifi_interface_t if_name) {
    wifi_mode_t mode;
    esp_err_t err = wifi_mode_switch(UNCHANGED, UNCHANGED, &mode);
    if (!err) {
        if (if_name == WIFI_IF_AP && !HAS_AP(mode)) {
            puts("AP not enabled");
            err = ESP_ERR_INVALID_STATE;
        }
        if (if_name == WIFI_IF_STA && !HAS_STA(mode)) {
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
            xEventGroupClearBits(evtgrp, WIFI_FAILED_BIT);
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
            } else if (evt->reason == WIFI_REASON_NO_AP_FOUND || retry > 5) {
                retry = 0;
                ESP_LOGW(TAG, "STA connect `%s` failed: 0x%02X", evt->ssid, evt->reason);
                xEventGroupSetBits(evtgrp, WIFI_FAILED_BIT);
                if (strbool(Config.net.AP_AUTO))
                    wifi_ap_start(NULL, NULL, NULL);
            } else  {
                retry++;
                esp_wifi_connect();
                ESP_LOGI(TAG, "STA connect `%s` retry %d", evt->ssid, retry);
            }
        } else if (id == WIFI_EVENT_SCAN_DONE) {
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
        } else {
            ESP_LOGD(TAG, "Unhandled %s 0x%04X %p", base, id, data);
        }
    } else {
        ESP_LOGD(TAG, "Unhandled %s 0x%04X %p", base, id, data);
    }
}

void wifi_initialize() {
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
    if (err) return err;
    wifi_scan_config_t scan_config = {
        .ssid = ssid,
        .channel = channel,
        .show_hidden = true
    };
    if (timeout_ms > 1300) {
        scan_config.scan_time.active.min = timeout_ms / 13;
        scan_config.scan_time.active.max = timeout_ms / 16;
        return esp_wifi_scan_start(&scan_config, true);
    } else {
        return esp_wifi_scan_start(&scan_config, false);
    }
}

esp_err_t wifi_sta_wait(uint16_t timeout_ms) {
    EventBits_t wait = WIFI_CONNECTED_BIT | WIFI_DISCONNECT_BIT | WIFI_FAILED_BIT;
    EventBits_t bits = xEventGroupWaitBits(
        evtgrp, wait, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    if (bits & WIFI_CONNECTED_BIT)
        return ESP_OK;
    if (bits & WIFI_DISCONNECT_BIT) {
        xEventGroupClearBits(evtgrp, WIFI_DISCONNECT_BIT);
        esp_wifi_connect();
        return wifi_sta_wait(timeout_ms);
    }
    if (bits & WIFI_FAILED_BIT)
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
    if (strlen(pass)) {
        ap->authmode = WIFI_AUTH_WPA_WPA2_PSK;
        snprintf((char *)ap->password, sizeof(ap->password), "%s", pass);
    } else {
        ap->authmode = WIFI_AUTH_OPEN;
        ap->password[0] = 0;
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
    } else if (bits & WIFI_FAILED_BIT) {
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
