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

static const char *TAG = "Wifi";

static EventGroupHandle_t evtgrp;

static esp_netif_t *sta, *ap;

static wifi_config_t ap_config = {
    .ap = {
        .channel = CONFIG_WIFI_CHANNEL,
        .max_connection = CONFIG_MAX_STA_CONN
    }
};

static wifi_config_t sta_config = {
    .sta = {
        .threshold.authmode = WIFI_AUTH_OPEN,
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH
    }
};

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    static int retry = 0;
    if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
            ESP_LOGI(TAG, "Got IP: " IPSTR ", GW: " IPSTR ", Mask: " IPSTR,
                    IP2STR(&evt->ip_info.ip),
                    IP2STR(&evt->ip_info.gw),
                    IP2STR(&evt->ip_info.netmask));
        }
    } else if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *evt = (wifi_event_ap_staconnected_t*)data;
            ESP_LOGI(TAG, "AP client " MACSTR " join, AID=%d, Mesh=%d",
                    MAC2STR(evt->mac), evt->aid, evt->is_mesh_child);
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *evt = (wifi_event_ap_stadisconnected_t*)data;
            ESP_LOGI(TAG, "AP client " MACSTR " leave, AID=%d, Mesh=%d",
                    MAC2STR(evt->mac), evt->aid, evt->is_mesh_child);
        } else if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)data;
            ESP_LOGI(TAG, "STA connect `%s` success", evt->ssid);
            xEventGroupSetBits(evtgrp, WIFI_CONNECTED_BIT);
            retry = 0;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;
            if (xEventGroupGetBits(evtgrp) & WIFI_DISCONNECT_BIT) {
                ESP_LOGI(TAG, "STA disconnect from `%s`", evt->ssid);
            } else if (evt->reason == WIFI_REASON_NO_AP_FOUND ||
                       evt->reason == WIFI_REASON_AUTH_FAIL || retry > 5)
            {
                ESP_LOGW(TAG, "STA connect `%s` failed: 0x%02X", evt->ssid, evt->reason);
                xEventGroupSetBits(evtgrp, WIFI_FAILED_BIT);
                ESP_LOGI(TAG, "Fallback to STA + AP");
                retry = 0;
                // TODO
            } else  {
                retry++;
                esp_wifi_connect();
                ESP_LOGD(TAG, "STA connect `%s` retry %d", evt->ssid, retry);
            }
        }
    } else {
        ESP_LOGD(TAG, "Unhandled event %s %d %p", base, id, data);
    }
}



void wifi_initialize() {
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);

    evtgrp = xEventGroupCreate();

    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    ap = esp_netif_create_default_wifi_ap();
    sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&init_config) );

    esp_event_base_t bases[2] = { WIFI_EVENT, IP_EVENT };
    for (int i = 0; i < 2; i++) {
        ESP_ERROR_CHECK( esp_event_handler_instance_register(
            bases[i], ESP_EVENT_ANY_ID, &event_handler, NULL, NULL
        ) );
    }

    const char *sta_ssid = Config.net.STA_SSID;
    const char *sta_pass = Config.net.STA_PASS;
    if (strlen(sta_ssid)) {
        wifi_sta_config_t *sta = &sta_config.sta;
        snprintf((char *)sta->ssid, sizeof(sta->ssid), "%s", sta_ssid);
        if (strlen(sta_pass)) {
            snprintf((char *)sta->password, sizeof(sta->password), "%s", sta_pass);
        } else {
            sta->password[0] = 0;
        }
        ESP_LOGI(TAG, "STA: connecting to `%s`", sta->ssid);
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
        ESP_ERROR_CHECK( esp_wifi_start() );
        if (!wifi_sta_wait(1000))
            return;
    }

    const char *ap_ssid = Config.net.AP_SSID;
    const char *ap_pass = Config.net.AP_PASS;
    if (strlen(ap_ssid)) {
        // Do NOT use strcpy/strncpy because we have to overwrite old values
        wifi_ap_config_t *ap = &ap_config.ap;
        size_t len = sizeof(ap->ssid);
        if (strlen(Config.info.UID)) {
            snprintf((char *)ap->ssid, len, "%s-%s", ap_ssid, Config.info.UID);
        } else {
            snprintf((char *)ap->ssid, len, "%s", ap_ssid);
        }
        ap->ssid_len = strlen((char *)ap->ssid);
        if (strlen(ap_pass)) {
            ap->authmode = WIFI_AUTH_WPA_WPA2_PSK;
            snprintf((char *)ap->password, sizeof(ap->password), "%s", ap_pass);
        } else {
            ap->authmode = WIFI_AUTH_OPEN;
            ap->password[0] = 0;
        }
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_config) );
        ESP_ERROR_CHECK( esp_wifi_start() );
        ESP_LOGI(TAG, "AP SSID %s, PASS %s, CH %d", ap_ssid, ap_pass, ap->channel);
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

esp_err_t wifi_sta_start(const char *ssid, const char *pass, const char *ip) {
    esp_err_t err = ESP_OK;
    if (ip && !( err = esp_netif_dhcpc_stop(sta) )) {
        esp_netif_ip_info_t sta_ip = {
            .ip.addr = ipaddr_addr(ip),
            .gw.addr = (ipaddr_addr(ip) & ~0xFF) | 0x01,
            .netmask.addr = ipaddr_addr("255.255.255.0")
        };
        if (( err = esp_netif_set_ip_info(sta, &sta_ip) )) {
            ESP_LOGE(TAG, "STA static IP failed: %s", esp_err_to_name(err));
            esp_netif_dhcpc_start(sta);
        }
    }
    return ESP_OK;
}

esp_err_t wifi_sta_stop() {
    xEventGroupSetBits(evtgrp, WIFI_DISCONNECT_BIT);
    esp_wifi_disconnect();
    return ESP_OK;
}

esp_err_t wifi_ap_start(const char *ssid, const char *pass, const char *ip) {
    return ESP_OK;
}

void wifi_sta_list_ap() {

}

void wifi_ap_list_sta() {
    uint16_t aid;
    wifi_sta_list_t stas;
    esp_err_t err = esp_wifi_ap_get_sta_list(&stas);
    if (err) {
        printf("Cannot get sta list: %s\n", esp_err_to_name(err));
        return;
    }
    if (!stas.num) {
        printf("No connected stations\n");
        return;
    }
    printf("AID  IP address\t MAC accress\t  RSSI Mode Mesh\n");
    for (int i = 0; i < stas.num; i++) {
        wifi_sta_info_t *hw = stas.sta + i;
        if (( err = esp_wifi_ap_get_sta_aid(hw->mac, &aid) )) {
            ESP_LOGD(TAG, "Get STA AID failed: %s", esp_err_to_name(err));
            continue;
        }
        printf("%04X %-16s" MACSTR " %4d %c%c%c%c %s\n",
            aid, "TODO", MAC2STR(hw->mac), hw->rssi,
            hw->phy_11b ? 'b' : ' ', hw->phy_11g ? 'g' : ' ',
            hw->phy_11n ? 'n' : ' ', hw->phy_lr ? 'L' : 'H',
            hw->is_mesh_child ? "true" : "false");
    }
}
