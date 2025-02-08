/*
 * File: network.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-21 13:33:54
 */

#include "network.h"
#include "server.h"
#include "config.h"
#include "update.h"
#include "timesync.h"

#ifdef CONFIG_BASE_USE_WIFI

#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "mdns.h"               // for mdns command
#include "esp_sntp.h"           // for sntp command
#include "ping/ping_sock.h"     // for ping command
#include "iperf.h"              // for iperf command

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAILURE_BIT    BIT1
#define WIFI_DISCONNECT_BIT BIT2
#define WIFI_SCANNING_BIT   BIT3
#define WIFI_SCAN_BLOCK_BIT BIT4
#define FTM_REPORT_BIT      BIT5
#define FTM_FAILURE_BIT     BIT6
#define TSS_STOP_BIT        BIT7
#define TSC_STOP_BIT        BIT8
#define TS_STOPPED_BIT      BIT9

#define HAS_STA(m)  ( m == WIFI_MODE_STA || m == WIFI_MODE_APSTA )
#define HAS_AP(m)   ( m == WIFI_MODE_AP || m == WIFI_MODE_APSTA )

static const char *TAG = "Network";

/******************************************************************************
 * Wraps on ESP WiFi APIs
 */

static EventGroupHandle_t evtgrp = NULL;

static esp_netif_t *if_sta = NULL, *if_ap = NULL;

static wifi_config_t config_ap = {
    .ap = {
        .channel = CONFIG_BASE_AP_CHANNEL,
        .max_connection = CONFIG_BASE_AP_MAX_CONN,
        .ftm_responder = true
    }
};

static wifi_config_t config_sta = {
    .sta = {
        .sae_pwe_h2e = WPA3_SAE_PWE_BOTH
    }
};

static EventBits_t waitBits(EventBits_t bits, uint32_t ms) {
    // ClearOnExit = false, WaitForAllBits = false
    return evtgrp ? xEventGroupWaitBits(
        evtgrp, bits, pdFALSE, pdFALSE, TIMEOUT(ms)) & bits : false;
}

static EventBits_t getBits(EventBits_t bits) {
    return evtgrp ? xEventGroupGetBits(evtgrp) & bits : false;
}

static bool setBits(EventBits_t bits) {
    return evtgrp ? xEventGroupSetBits(evtgrp, bits) : false;
}

static EventBits_t clearBits(EventBits_t bits) {
    return evtgrp ? xEventGroupClearBits(evtgrp, bits) : 0;
}

static bool str_like_ipaddr(const char *s) {
    int len = strlen(s ?: ""), dot = 0, num = 0, hex = 0, con = 0;
    LOOPN(i, len) {
             if (s[i] == '.') { dot++; }
        else if (s[i] == ':') { con++; }
        else if ('0' <= s[i] && s[i] <= '9') { num++; }
        else if ('a' <= s[i] && s[i] <= 'f') { hex++; }
        else if ('A' <= s[i] && s[i] <= 'F') { hex++; }
    }
    if (dot == 3 && (num + dot) == len) return true;
    if (!dot && con && (con + num + hex) == len) return true;
    return false;
}

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
    default:                        return "Unknown";
    }
}

static const char * wifi_mode_str(wifi_mode_t mode) {
    switch (mode) {
    case WIFI_MODE_NULL:            return "NULL";
    case WIFI_MODE_STA:             return "STA";
    case WIFI_MODE_AP:              return "AP";
    case WIFI_MODE_APSTA:           return "AP+STA";
    default:                        return "Unknown";
    }
}

static const char * wifi_ftm_str(wifi_ftm_status_t status) {
    switch (status) {
    case FTM_STATUS_UNSUPPORTED:    return "Unsupported";
    case FTM_STATUS_CONF_REJECTED:  return "Config Rejected";
    case FTM_STATUS_NO_RESPONSE:    return "No Response";
    default:                        return "ERROR";
    }
}

#define UNCHANGED -1

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
        if (HAS_STA(mode) && getBits(WIFI_CONNECTED_BIT)) {
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

static void wifi_print_ipaddr(esp_netif_t * if_ptr, FILE *stream) {
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(if_ptr, &ip);
    const char *desc = esp_netif_get_desc(if_ptr);
    if (stream) {
        fprintf(stream, "IF %s IP " IPSTR ", GW " IPSTR ", Mask " IPSTR "\n",
                desc, IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
    } else {
        ESP_LOGI(TAG, "IF %s IP " IPSTR ", GW " IPSTR ", Mask " IPSTR,
                desc, IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
    }
}

static void wifi_print_apinfo(wifi_ap_record_t *aps, int num) {
    if (!num) return;
    size_t maxlen = 10;
    LOOPN(i, num) {
        if (!aps[i].country.cc[0]) {
            aps[i].country.cc[0] = aps[i].country.cc[1] = ' ';
        }
#ifdef CONFIG_BASE_AUTO_ALIGN
        maxlen = MAX(maxlen, strlen((char *)aps[i].ssid));
#else
        maxlen = 24;
#endif
    }
    printf("SSID%*s", maxlen - 4 + 1, "");
    puts("MAC address       RSSI Mode WPS FTM Auth   CC Channel");
    LOOPN(i, num) {
        const char *ftm = "";
        if (aps[i].ftm_responder && aps[i].ftm_initiator) {
            ftm = "yes";
        } else if (aps[i].ftm_responder) {
            ftm = "REP";
        } else if (aps[i].ftm_initiator) {
            ftm = "REQ";
        }
        printf("%-*s " MACSTR "  %-3d %c%c%c%c %3s %3s %-6s %c%c %2d",
            maxlen, (char *)aps[i].ssid, MAC2STR(aps[i].bssid), aps[i].rssi,
            aps[i].phy_11b ? 'b' : ' ', aps[i].phy_11g ? 'g' : ' ',
            aps[i].phy_11n ? 'n' : ' ', aps[i].phy_lr ? 'l' : 'h',
            aps[i].wps ? "yes" : "", ftm, wifi_authmode_str(aps[i].authmode),
            aps[i].country.cc[0], aps[i].country.cc[1], aps[i].primary);
        if (aps[i].country.nchan)
            printf(" (%d-%d)", aps[i].country.schan, aps[i].country.nchan);
        putchar('\n');
    }
}

static uint16_t s_nap = 0;
static wifi_ap_record_t *s_aps = NULL;

static esp_err_t wifi_get_ap_records() {
    // Must call this function after sta scan
    uint16_t nap;
    wifi_ap_record_t *aps = NULL;
    esp_err_t err = esp_wifi_scan_get_ap_num(&nap);
    if (!err) err = nap ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (!err) err = ECALLOC(aps, nap, sizeof(wifi_ap_record_t));
    if (!err) err = esp_wifi_scan_get_ap_records(&nap, aps);
    if (!err) {
        TRYFREE(s_aps);
        s_aps = aps;
        s_nap = nap;
    } else {
        TRYFREE(aps);
    }
    return err;
}

static esp_err_t UNUSED wifi_find_ap_record(
    const char *ssid, uint8_t *bssid, bool scan, wifi_ap_record_t *record
) {
    bool found = false;
    esp_err_t err = ESP_OK;
    LOOPN(i, s_nap) {
        if (
            (ssid && !strcmp((char *)s_aps[i].ssid, ssid)) ||
            (bssid && !memcmp(s_aps[i].bssid, bssid, sizeof(s_aps[i].bssid)))
        ) {
            *record = s_aps[i]; // copy whole structure
            found = true;
            break;
        }
    }
    if (found) return ESP_OK;
    if (!scan) return ESP_ERR_NOT_FOUND;
    if (( err = wifi_sta_scan(ssid, 0, 1500, false) )) return err;
    return wifi_find_ap_record(ssid, bssid, false, record);
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

static void cb_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    static int update = 0;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = data;
        if (evt->ip_changed) wifi_print_ipaddr(evt->esp_netif, NULL);
        if (strbool(Config.app.OTA_AUTO) && !update) {
            ota_updation_url(NULL, false);
            update = 0;
        }
    } else if (id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *evt = data;
        ESP_LOGI(TAG, "AP client " IPSTR " assigned", IP2STR(&evt->ip));
    } else {
        ESP_LOGD(TAG, "Unhandled %s 0x%04X %p", base, id, data);
    }
}

static void cb_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    static int retry = 0;
    // For sys_evt stack overflow, try to uncomment next line:
    // ESP_LOGD(TAG, "event stack %d", uxTaskGetStackHighWaterMark(NULL));
    if (id == WIFI_EVENT_AP_START) {
        wifi_config_t cfg;
        esp_wifi_get_config(WIFI_IF_AP, &cfg);
        ESP_LOGI(TAG, "AP SSID %s, PASS %s, Channel %d",
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
        setBits(WIFI_CONNECTED_BIT);
        clearBits(WIFI_FAILURE_BIT | WIFI_DISCONNECT_BIT);
        wifi_event_sta_connected_t *evt = data;
        ESP_LOGI(TAG, "STA connect `%s` success", evt->ssid);
        retry = 0;
        if (strbool(Config.net.AP_AUTO)) wifi_ap_stop();
        if (strbool(Config.app.SNTP_RUN)) sntp_control("sync");
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        clearBits(WIFI_CONNECTED_BIT);
        wifi_event_sta_disconnected_t *evt = data;
        if (getBits(WIFI_DISCONNECT_BIT)) {
            ESP_LOGI(TAG, "STA disconnect from `%s`", evt->ssid);
            clearBits(WIFI_DISCONNECT_BIT);
        } else if (evt->reason == WIFI_REASON_NO_AP_FOUND || retry > 2) {
            retry = 0;
            ESP_LOGW(TAG, "STA connect `%s` failed: %d", evt->ssid, evt->reason);
            setBits(WIFI_FAILURE_BIT);
            if (strbool(Config.net.AP_AUTO)) wifi_ap_start(NULL, NULL, NULL);
        } else  {
            retry++;
            esp_wifi_connect();
            ESP_LOGD(TAG, "STA connect `%s` retry %d", evt->ssid, retry);
        }
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        clearBits(WIFI_SCANNING_BIT);
        if (getBits(WIFI_SCAN_BLOCK_BIT)) {
            clearBits(WIFI_SCAN_BLOCK_BIT);
        } else if (!wifi_get_ap_records()) {
            ESP_LOGI(TAG, "STA found %d AP", s_nap);
            wifi_print_apinfo(s_aps, s_nap);
        }
    } else if (id == WIFI_EVENT_FTM_REPORT) {
        wifi_event_ftm_report_t *evt = data;
        if (evt->status != FTM_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "FTM " MACSTR " failed: %s",
                    MAC2STR(evt->peer_mac), wifi_ftm_str(evt->status));
            setBits(FTM_FAILURE_BIT);
        } else {
            ESP_LOGI(TAG, "FTM " MACSTR " RTT %dns, Dist %dcm",
                    MAC2STR(evt->peer_mac), evt->rtt_est, evt->dist_est);
            if (evt->ftm_report_num_entries) puts("ID  RSSI RTT (ns)");
            LOOPN(i, evt->ftm_report_num_entries) {
                wifi_ftm_report_entry_t *ent = evt->ftm_report_data + i;
                printf("%-3d %-3d %7.3f\n",
                        ent->dlog_token, ent->rssi, ent->rtt / 1e3);
            }
            TRYFREE(evt->ftm_report_data);
            setBits(FTM_REPORT_BIT);
        }
    } else {
        ESP_LOGD(TAG, "Unhandled %s 0x%04X %p", base, id, data);
    }
}

void network_initialize() {
    const char * tags[] = {
        "wifi", "wifi_init", "iperf",
        "esp_netif_lwip", "esp_netif_handlers",
    };
    LOOPN(i, LEN(tags)) { esp_log_level_set(tags[i], ESP_LOG_WARN); }

    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    if (!if_ap) if_ap = esp_netif_create_default_wifi_ap();
    if (!if_sta) if_sta = esp_netif_create_default_wifi_sta();
    if (!evtgrp) evtgrp = xEventGroupCreate();

#define REGEVT(evt, cb) \
    esp_event_handler_instance_register(evt, ESP_EVENT_ANY_ID, cb, NULL, NULL)
    ESP_ERROR_CHECK( REGEVT(WIFI_EVENT, &cb_wifi) );
    ESP_ERROR_CHECK( REGEVT(IP_EVENT, &cb_ip) );

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&init_config) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    esp_err_t err;
    if (strbool(Config.app.MDNS_RUN) && ( err = mdns_control("on") ))
        ESP_LOGE(TAG, "Failed to start mDNS: %s", esp_err_to_name(err));
    if (strbool(Config.app.SNTP_RUN) && ( err = sntp_control("on") ))
        ESP_LOGE(TAG, "Failed to start SNTP: %s", esp_err_to_name(err));
    if (!( err = wifi_sta_start(NULL, NULL, NULL) )) return;
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
    if (getBits(WIFI_CONNECTED_BIT)) {
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
            .ip.addr = inet_addr(ip),
            .gw.addr = (inet_addr(ip) & ~0xFF) | 0x01,
            .netmask.addr = inet_addr("255.255.255.0")
        };
        if (( err = esp_netif_set_ip_info(if_sta, &ip_sta) )) {
            ESP_LOGE(TAG, "STA static IP failed: %s", esp_err_to_name(err));
            wifi_dhcp_switch(true, UNCHANGED);
        } else {
            ESP_LOGI(TAG, "STA static IP set to %s", ip);
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
    setBits(WIFI_DISCONNECT_BIT);
    return esp_wifi_disconnect();
}

esp_err_t wifi_sta_scan(
    const char * ssid, uint8_t channel, uint16_t tout_ms, bool verbose
) {
    esp_err_t err = wifi_mode_check(WIFI_IF_STA);
    if (!err) err = esp_wifi_scan_stop();
    if (!err) err = esp_wifi_clear_ap_list();
    if (err) return err;
    wifi_scan_config_t scan_config = {
        .ssid = (uint8_t *)ssid,
        .channel = channel,
        .show_hidden = true
    };
    if (!tout_ms) {
        setBits(WIFI_SCANNING_BIT);
        return esp_wifi_scan_start(&scan_config, false);
    }
    scan_config.scan_time.active.min = CONS(tout_ms / 15, 10, 1400);
    scan_config.scan_time.active.max = CONS(tout_ms / 10, 20, 1500);
    setBits(WIFI_SCANNING_BIT | WIFI_SCAN_BLOCK_BIT);
    if (
        !( err = esp_wifi_scan_start(&scan_config, true) ) &&
        !( err = wifi_get_ap_records() ) &&
        verbose
    ) {
        ESP_LOGI(TAG, "STA found %d AP", s_nap);
        wifi_print_apinfo(s_aps, s_nap);
    }
    return err;
}

esp_err_t wifi_sta_wait(uint16_t tout_ms) {
    EventBits_t bits = waitBits(
        WIFI_CONNECTED_BIT | WIFI_DISCONNECT_BIT | WIFI_FAILURE_BIT, tout_ms);
    if (bits & WIFI_FAILURE_BIT) return ESP_FAIL;
    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;
    if (bits & WIFI_DISCONNECT_BIT) {
        ESP_LOGW(TAG, "STA stopped by wifi_sta_stop");
        return ESP_OK;
    }
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
            .ip.addr = inet_addr(ip),
            .gw.addr = inet_addr(ip),
            .netmask.addr = inet_addr("255.255.255.0")
        };
        if (( err = esp_netif_set_ip_info(if_ap, &ip_ap) ))
            ESP_LOGE(TAG, "AP static IP failed: %s", esp_err_to_name(err));
        wifi_dhcp_switch(UNCHANGED, true);
    }

    wifi_ap_config_t *ap = &config_ap.ap;
    snprintf((char *)ap->ssid, sizeof(ap->ssid), "%s", ssid);
    if (strlen(Config.info.UID)) {
        snprintf((char *)ap->ssid + strlen(ssid),
                sizeof(ap->ssid) - strlen(ssid), "-%s", Config.info.UID);
    }
    ap->ssid_len = strlen((char *)ap->ssid);
    if (!pass || strlen(pass) < 8) {
        ap->authmode = WIFI_AUTH_OPEN;
        ap->password[0] = 0;
    } else {
        ap->authmode = WIFI_AUTH_WPA_WPA2_PSK;
        snprintf((char *)ap->password, sizeof(ap->password), "%s", pass);
    }
    ap->ssid_hidden = strbool(Config.net.AP_HIDE);
    return esp_wifi_set_config(WIFI_IF_AP, &config_ap);
}

esp_err_t wifi_ap_stop() { return wifi_mode_switch(UNCHANGED, false, NULL); }

esp_err_t wifi_sta_list_ap() {
    esp_err_t err = wifi_mode_check(WIFI_IF_STA);
    if (err == ESP_ERR_INVALID_STATE) return ESP_OK;
    if (err) return err;
    if (config_sta.sta.ssid && strlen((char *)config_sta.sta.ssid)) {
        printf("STA SSID %s ", (char *)config_sta.sta.ssid);
    } else {
        printf("STA ");
    }
    if (getBits(WIFI_DISCONNECT_BIT)) {
        puts("disconnected");
    } else if (getBits(WIFI_FAILURE_BIT)) {
        puts("failed");
    } else if (getBits(WIFI_CONNECTED_BIT)) {
        puts("connected");
        wifi_print_ipaddr(if_sta, stdout);
        wifi_ap_record_t info;
        if (s_nap) {
            wifi_print_apinfo(s_aps, s_nap);
        } else if (!esp_wifi_sta_get_ap_info(&info)) {
            wifi_print_apinfo(&info, 1);
        }
    } else {
        puts("not initialized");
    }
    return ESP_OK;
}

esp_err_t wifi_ap_list_sta() {
    esp_err_t err = wifi_mode_check(WIFI_IF_AP);
    if (err == ESP_ERR_INVALID_STATE) return ESP_OK;
    if (err) return err;
    printf("AP SSID %s Channel %d\n", config_ap.ap.ssid, config_ap.ap.channel);
    wifi_print_ipaddr(if_ap, stdout);

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
        printf("%-4d %-16s " MACSTR " %4d %c%c%c%c %s\n",
            aid, inet_ntoa(sw->ip), MAC2STR(sw->mac), hw->rssi,
            hw->phy_11b ? 'b' : ' ', hw->phy_11g ? 'g' : ' ',
            hw->phy_11n ? 'n' : ' ', hw->phy_lr ? 'l' : 'h',
            hw->is_mesh_child ? "true" : "false");
    }
    return err;
}

esp_err_t wifi_parse_addr(const char *host, void *dst) {
    ip_addr_t tmp = { 0 };
    struct addrinfo *res = NULL, *ptr;
    if (getaddrinfo(host, NULL, NULL, &res) != 0) return ESP_ERR_INVALID_ARG;
    for (ptr = res; ptr; ptr = ptr->ai_next) {
        if (ptr->ai_family == AF_INET) {
            struct sockaddr_in *addr4 = (struct sockaddr_in *)ptr->ai_addr;
            inet_addr_to_ip4addr(ip_2_ip4(&tmp), &addr4->sin_addr);
        } else {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ptr->ai_addr;
            inet6_addr_to_ip6addr(ip_2_ip6(&tmp), &addr6->sin6_addr);
        }
        if (!dst) {
            puts(ipaddr_ntoa(&tmp));
        } else if (ptr == res) {
            *(ip_addr_t *)dst = tmp;
        }
    }
    freeaddrinfo(res);
    return ESP_OK;
}

/******************************************************************************
 * Applications based on network
 */

esp_err_t ftm_respond(const char *ctrl, int16_t offset_cm) {
#ifdef CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
    esp_err_t err = wifi_mode_check(WIFI_IF_AP);
    if (err) return err;
    if (offset_cm && !( err = esp_wifi_ftm_resp_set_offset(offset_cm) )) {
        ESP_LOGI(TAG, "FTM responder set offset to %dcm", offset_cm);
    } else if (err) {
        ESP_LOGW(TAG, "FTM responder offset failed: %s", esp_err_to_name(err));
        return err;
    }
    if (ctrl && strbool(ctrl) != config_ap.ap.ftm_responder) {
        config_ap.ap.ftm_responder = !config_ap.ap.ftm_responder;
        if (( err = esp_wifi_set_config(WIFI_IF_AP, &config_ap) )) return err;
    }
    ESP_LOGI(TAG, "FTM responder %s",
            config_ap.ap.ftm_responder ? "enabled" : "disabled");
    return err;
#else
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ctrl); NOTUSED(offset_cm);
#endif
}

esp_err_t ftm_request(const char *ssid, uint8_t count) {
#ifdef CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    esp_err_t err = wifi_mode_check(WIFI_IF_STA);
    if (err) return err;

    wifi_ap_record_t record;
    if (ssid) {
        if (( err = wifi_find_ap_record(ssid, NULL, true, &record) ))
            return err;
    } else if (getBits(WIFI_CONNECTED_BIT)) {
        if (( err = esp_wifi_sta_get_ap_info(&record) ))
            return err;
    } else {
        ESP_LOGE(TAG, "STA disconnected. Provide SSID to run FTM.");
        return ESP_ERR_INVALID_ARG;
    }
    if (!record.ftm_responder) {
        ESP_LOGE(TAG, "FTM not supported by `%s`", (char *)record.ssid);
        return ESP_ERR_INVALID_ARG;
    }
    wifi_ftm_initiator_cfg_t config = {
        .channel = record.primary,
        .frm_count = 32,
        .burst_period = 2   // 200ms
    };
    memcpy(config.resp_mac, record.bssid, sizeof(config.resp_mac));
    if (!(count % 8) && (count <= 32 || count == 64)) config.frm_count = count;
    ESP_LOGI(TAG, "FTM " MACSTR " channel=%d count=%d period=%dms",
            MAC2STR(config.resp_mac), config.channel,
            config.frm_count, config.burst_period * 100);
    if (( err = esp_wifi_ftm_initiate_session(&config) )) return err;
    EventBits_t bits = waitBits(FTM_REPORT_BIT | FTM_FAILURE_BIT, 3000);
    clearBits(bits);
    if (bits & FTM_FAILURE_BIT) return ESP_FAIL;
    if (bits & FTM_REPORT_BIT) return ESP_OK;
    esp_wifi_ftm_end_session();
    return ESP_ERR_TIMEOUT;
#else
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ssid); NOTUSED(count);
#endif
}

static const char * tcpip_if_str(tcpip_adapter_if_t interface) {
    switch (interface) {
    case TCPIP_ADAPTER_IF_STA:      return "STA";
    case TCPIP_ADAPTER_IF_AP:       return "AP";
    case TCPIP_ADAPTER_IF_ETH:      return "ETH";
    default:                        return "Unknown";
    }
}

static void mdns_print_results(mdns_result_t *r) {
    for (int i = 1; r; i++, r = r->next) {
        printf("%d: Interface: %s TTL %u IPv%c\n",
                i, tcpip_if_str(r->tcpip_if), r->ttl,
                r->ip_protocol ? '6' : '4');
        if (r->instance_name)
            printf("  PTR : %s.%s.%s\n",
                   r->instance_name, r->service_type, r->proto);
        if (r->hostname) printf("  SRV : %s.local:%u\n", r->hostname, r->port);
        if (r->txt_count) {
            printf("  TXT : [%u] ", r->txt_count);
            LOOPN(t, r->txt_count) {
                printf("%s=%s(%d); ",
                    r->txt[t].key,
                    r->txt[t].value ?: "NULL",
                    r->txt_value_len[t]);
            }
            putchar('\n');
        }
        for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
            if (a->addr.type == IPADDR_TYPE_V6) {
                printf("  AAAA: " IPV6STR "\n", IPV62STR(a->addr.u_addr.ip6));
            } else {
                printf("  A   : " IPSTR "\n", IP2STR(&a->addr.u_addr.ip4));
            }
        }
    }
}

static esp_err_t mdns_query_service(
    const char *service, const char *proto, uint16_t tout_ms
) {
    ESP_LOGI(TAG, "Query PTR: %s.%s.local", service, proto);
    mdns_result_t *results = NULL;
    esp_err_t err = mdns_query_ptr(service, proto, tout_ms, 20, &results);
    if (err) {
        ESP_LOGE(TAG, "Query failed: %s", esp_err_to_name(err));
    } else if (!results) {
        ESP_LOGW(TAG, "No services found: %s.%s", service, proto);
    } else {
        mdns_print_results(results);
        mdns_query_results_free(results);
    }
    return err;
}

static esp_err_t mdns_query_host(const char *hostname, uint16_t tout_ms) {
    ESP_LOGI(TAG, "Query A: %s.local", hostname);
    struct esp_ip4_addr addr = { .addr = 0 };
    esp_err_t err = mdns_query_a(hostname, tout_ms, &addr);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Host not found: %s", hostname);
    } else if (err) {
        ESP_LOGE(TAG, "Query failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Found %s.local at " IPSTR, hostname, IP2STR(&addr));
    }
    return err;
}

static esp_err_t mdns_initialize() {
    char hostname[32] = { 0 };
    snprintf(hostname, sizeof(hostname), "%s-%s",
             strlen(Config.app.MDNS_HOST)
             ? Config.app.MDNS_HOST
             : Config.info.NAME,
             Config.info.UID);
    esp_err_t err = mdns_init();
    if (!err) err = mdns_hostname_set(hostname);
    if (!err) err = mdns_instance_name_set("ESP32 mDNS");
#ifdef CONFIG_BASE_USE_WEBSERVER
    if (!err) {
        mdns_txt_item_t desc[] = {
            { "name", Config.info.NAME },
            { "ver", Config.info.VER },
            { "uid", Config.info.UID }
        };
        err = mdns_service_add(hostname, "_http", "_tcp", 80, desc, LEN(desc));
    }
#endif
    return err;
}

esp_err_t mdns_command(
    const char *ctrl, const char *hostname,
    const char *service, const char *proto, uint16_t tout_ms
) {
    static bool mdns_running = false;
    esp_err_t err = ESP_OK;
    if (ctrl) {
        if (!strbool(ctrl)) {
            mdns_free();
            mdns_running = false;
        } else if (!mdns_running) {
            err = mdns_initialize();
            mdns_running = !err;
        }
    } else if (hostname) {
        err = mdns_query_host(hostname, tout_ms ?: 2000);
    } else if (service || proto) {
        char sbuf[32] = "_http", pbuf[32] = "_tcp";
        if (service)
            snprintf(sbuf, sizeof(sbuf), "%s%s",
                startswith(service, "_") ? "" : "_", service);
        if (proto)
            snprintf(pbuf, sizeof(pbuf), "%s%s",
                startswith(proto, "_") ? "" : "_", proto);
        err = mdns_query_service(sbuf, pbuf, tout_ms ?: 3000);
    } else {
        printf("mDNS: %s\n", mdns_running ? "enabled" : "disabled");
    }
    return err;
}

static void sntp_timesync_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP time synced: %s", format_timestamp_us(tv));
}

static const char * sntp_status_str(sntp_sync_status_t status) {
    if (status == SNTP_SYNC_STATUS_RESET) return "reset";
    if (status == SNTP_SYNC_STATUS_COMPLETED) return "completed";
    if (status == SNTP_SYNC_STATUS_IN_PROGRESS) return "in progress";
    return "Unknown";
}

static esp_err_t sntp_setserverhost(uint8_t idx, const char *host) {
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (idx > SNTP_MAX_SERVERS || !strlen(host ?: "")) return err;
    ip_addr_t addr = { 0 };
    err = wifi_parse_addr(host, &addr);
    if (getBits(WIFI_CONNECTED_BIT) && err) return err;
    bool updated = false;
#if SNTP_SERVER_DNS
    if (!str_like_ipaddr(host)) {
        sntp_setservername(idx, host);
        updated = true;
    } else
#endif
    if (!err) {
        sntp_setserver(idx, &addr);
        updated = true;
    }
#if LWIP_DHCP_GET_NTP_SRV
    if (updated) {
        sntp_servermode_dhcp(0);
    } else if (ip_addr_isany(sntp_getserver(idx))) {
        sntp_servermode_dhcp(1);
    }
#endif
    if (updated) ESP_LOGI(TAG, "SNTP server#%d set to %s", idx, host);
    return err;
}

esp_err_t sntp_command(
    const char *ctrl, const char *host, const char *mode, uint32_t intv_ms
) {
    if (ctrl) {
        if (strcasestr(ctrl, "reset") || strcasestr(ctrl, "sync")) {
            sntp_restart();
        } else if (strbool(ctrl) && !sntp_enabled()) {
            sntp_set_time_sync_notification_cb(sntp_timesync_cb);
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setserverhost(0, Config.app.SNTP_HOST);
            sntp_init();
        } else if (!strbool(ctrl)) {
            sntp_stop();
        }
    } else if (host) {
        int idx = SNTP_MAX_SERVERS - 1;
        LOOPN(i, SNTP_MAX_SERVERS) {
            if (ip_addr_isany(sntp_getserver(i))) idx = i;
        }
        sntp_setserverhost(idx, host);
    } else if (mode) {
        if (strcasestr(mode, "smooth")) {
            sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
        } else {
            sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        }
    } else if (intv_ms && intv_ms != sntp_get_sync_interval()) {
        sntp_set_sync_interval(intv_ms);
    } else {
        printf("SNTP: %s\n", sntp_enabled() ? "enabled" : "disabled");
        if (sntp_enabled()) {
            printf(
                "Status:   %s\n"
                "SyncMode: %s\n"
                "OperMode: %s\n"
                "Interval: %ds\n"
                "Datetime: %s\n",
                sntp_status_str(sntp_get_sync_status()),
                sntp_get_sync_mode() ? "smooth" : "immediate",
                sntp_getoperatingmode() ? "listen only" : "poll",
                sntp_get_sync_interval() / 1000,
                format_datetime_us(NULL));
            LOOPN(i, SNTP_MAX_SERVERS) {
                const ip_addr_t *addr = sntp_getserver(i);
                if (ip_addr_isany(addr)) continue;
                printf("Server#%d: %s", i, ipaddr_ntoa(addr));
#if SNTP_SERVER_DNS
                const char *name = sntp_getservername(i);
                if (name) printf(" (%s)", name);
#endif
#if SNTP_MONITOR_SERVER_REACHABILITY
                if (!sntp_getreachability(i)) printf(" (unreachable)");
#endif
                putchar('\n');
            }
        }
    }
    return ESP_OK;
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
    printf("%d bytes from %s icmp_seq=%d ttl=%d time=%dms\n",
           size, ipaddr_ntoa(&target), seqno, ttl, dtms);
}

static void ping_command_timeout(esp_ping_handle_t hdl, void *args) {
    uint16_t seqno;
    ip_addr_t target;
    GET_PING_PROF(SEQNO, seqno);
    GET_PING_PROF(IPADDR, target);
    printf("From %s: icmp_seq=%d timeout\n", ipaddr_ntoa(&target), seqno);
}

static void ping_command_end(esp_ping_handle_t hdl, void *ptr) {
    ip_addr_t target;
    uint32_t sent, recv, dtms;
    GET_PING_PROF(REPLY, recv);
    GET_PING_PROF(REQUEST, sent);
    GET_PING_PROF(DURATION, dtms);
    GET_PING_PROF(IPADDR, target);
    printf("Ping %s: %d sent, %d recv, %d lost (%d%%) in %dms\n",
           ipaddr_ntoa(&target), sent, recv, sent - recv,
           100 * (sent - recv) / (sent ?: 1), dtms);
    esp_ping_delete_session(hdl);
    if (ptr) *(esp_ping_handle_t *)ptr = NULL;
}

esp_err_t ping_command(
    const char *host, uint16_t intv, uint16_t size, uint16_t count, bool abort
) {
    static esp_ping_handle_t hdl = NULL;
    if (abort) {
        if (hdl) {
            esp_ping_stop(hdl);
            puts("Ping session stopped");
            esp_ping_delete_session(hdl);
            hdl = NULL;
        }
        return ESP_OK;
    } else if (!host || hdl) {
        printf("Ping session %srunning\n", hdl ? "" : "not ");
        return hdl ? ESP_ERR_INVALID_STATE : ESP_OK;
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    if (wifi_parse_addr(host, &config.target_addr)) {
        printf("Invalid host to ping: %s\n", host);
        return ESP_ERR_INVALID_ARG;
    }
    if (intv)   config.interval_ms = intv;
    if (size)   config.data_size = size;
    if (count)  config.count = count;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_command_success,
        .on_ping_timeout = ping_command_timeout,
        .on_ping_end = ping_command_end,
        .cb_args = &hdl
    };
    esp_err_t err = esp_ping_new_session(&config, &cbs, &hdl);
    if (!err) err = esp_ping_start(hdl);
    if (err) {
        TRYNULL(hdl, esp_ping_delete_session);
    } else if (intv && config.count != ESP_PING_COUNT_INFINITE) {
        msleep(intv * config.count + 10);
    }
    return err;
}

esp_err_t iperf_command(
    const char *host, uint16_t port, uint16_t length,
    uint8_t intv_sec, uint8_t tout_sec, bool udp, bool abort
) {
    TaskHandle_t server = xTaskGetHandle(IPERF_TRAFFIC_TASK_NAME);
    TaskHandle_t client = xTaskGetHandle(IPERF_REPORT_TASK_NAME);
    if (abort) {
        if (server) puts("IPerf server stopped");
        if (client) puts("IPerf client stopped");
        return iperf_stop();
    } else if (server || client) {
        printf("IPerf %s running\n", server ? "server" : "client");
        return ESP_OK;
    } else if (host && !strlen(host)) {
        puts("IPerf not running");
        return ESP_OK;
    }

    uint32_t flag = host ? IPERF_FLAG_CLIENT : IPERF_FLAG_SERVER;
    uint32_t src_ip = wifi_local_ip(NULL);
    uint32_t dst_ip = ipaddr_addr(host ?: "");
    esp_err_t err = src_ip ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!err && host && dst_ip == IPADDR_NONE) err = ESP_ERR_INVALID_ARG;
    if (err) return err;

    iperf_cfg_t config = {
        .flag = flag | (udp ? IPERF_FLAG_UDP : IPERF_FLAG_TCP),
        .destination_ip4 = host ? dst_ip : 0,
        .source_ip4 = src_ip,
        .type = IPERF_IP_TYPE_IPV4, // currently only support IPv4
        .dport = (port && host) ? port : IPERF_DEFAULT_PORT,
        .sport = (port && !host) ? port : IPERF_DEFAULT_PORT,
        .interval = intv_sec ?: IPERF_DEFAULT_INTERVAL,
        .time = (tout_sec ?: IPERF_DEFAULT_TIME) + (host ? 0 : 1),
        .len_send_buf = length,
        .bw_lim = IPERF_DEFAULT_NO_BW_LIMIT
    };
    if (config.time < config.interval) config.time = config.interval;
    if (!( err = iperf_start(&config) )) {
        size_t blen = IP4ADDR_STRLEN_MAX;
        char sip[blen], dip[blen];
        ESP_LOGI(TAG, "mode=%s-%s sip=%s:%d, dip=%s:%d, intval=%d, tout=%d",
            udp ? "udp" : "tcp", host ? "client" : "server",
            inet_ntoa_r(config.source_ip4, sip, blen), config.sport,
            inet_ntoa_r(config.destination_ip4, dip, blen), config.dport,
            config.interval, config.time);
    }
    return err;
}

typedef struct {
    const char * host;
    uint16_t port;
    uint32_t tout;
} timesync_param_t;

static void timesync_server_task(void *arg) {
    timesync_param_t param = *(timesync_param_t *)arg;
    if (timesync_server_init(param.port)) {
        ESP_LOGE(TAG, "Could not init TimeSync Server");
    } else {
        clearBits(TSS_STOP_BIT);
        while (!getBits(TSS_STOP_BIT)) {
            timesync_server_loop(param.tout);
        }
    }
    timesync_server_exit();
    setBits(TS_STOPPED_BIT | TSS_STOP_BIT);
    vTaskDelete(NULL);
}

static void timesync_client_task(void *arg) {
    timesync_param_t param = *(timesync_param_t *)arg;
    if (timesync_client_init(param.host, param.port)) {
        ESP_LOGE(TAG, "Could not init TimeSync Client");
    } else {
        double offset = 0;
        clearBits(TSC_STOP_BIT);
        while (!getBits(TSC_STOP_BIT)) {
            if (timesync_client_xsync(&offset, 5) < 0) break;
            ESP_LOGI(TAG, "TimeSync offset with %s: %.9f", param.host, offset);
            waitBits(TSC_STOP_BIT, param.tout);
        }
    }
    timesync_client_exit();
    setBits(TS_STOPPED_BIT | TSC_STOP_BIT);
    vTaskDelete(NULL);
}

esp_err_t tsync_command(
    const char *host, uint16_t port, uint32_t tout_ms, bool abort
) {
    TaskHandle_t server = xTaskGetHandle("tss");
    TaskHandle_t client = xTaskGetHandle("tsc");
    if (abort) {
        if (server) puts("TimeSync server stopped");
        if (client) puts("TimeSync client stopped");
        setBits(TSS_STOP_BIT | TSC_STOP_BIT);
        waitBits(TS_STOPPED_BIT, tout_ms ?: 1000);
        return ESP_OK; // do NOT use vTaskDelete because we need clean on exit
    } else if (server || client) {
        printf("TimeSync %s running\n", server ? "server" : "client");
        return ESP_OK;
    } else if (host && !strlen(host)) {
        puts("TimeSync not running");
        return ESP_OK;
    }

    timesync_param_t param = {
        .host = host,
        .port = port ?: TIMESYNC_PORT,
        .tout = tout_ms ?: (host ? 30000 : 500),
    };
    TaskHandle_t task = NULL;
    clearBits(TS_STOPPED_BIT);
    if (host) {
        xTaskCreate(timesync_client_task, "tsc", 4096, &param, 5, &task);
    } else {
        xTaskCreate(timesync_server_task, "tss", 4096, &param, 5, &task);
    }
    if (!task) return ESP_ERR_NO_MEM;
    return waitBits(TS_STOPPED_BIT, 50) ? ESP_FAIL : ESP_OK;
}

#else // CONFIG_BASE_USE_WIFI

void network_initialize() {};

esp_err_t wifi_ap_start(const char *s, const char *p, const char *i) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(s); NOTUSED(p); NOTUSED(i);
}
esp_err_t wifi_ap_stop() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t wifi_ap_list_sta() { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t wifi_sta_start(const char *s, const char *p, const char *i) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(s); NOTUSED(p); NOTUSED(i);
}
esp_err_t wifi_sta_stop() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t wifi_sta_scan(const char *s, uint8_t c, uint16_t t, bool v) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(s); NOTUSED(c); NOTUSED(t); NOTUSED(v);
}
esp_err_t wifi_sta_wait(uint16_t t) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(t);
}
esp_err_t wifi_sta_list_ap() { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t wifi_parse_addr(const char *h, void *d) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(h); NOTUSED(d);
}

esp_err_t ftm_respond(const char *c, int16_t o) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c); NOTUSED(o);
}
esp_err_t ftm_request(const char *s, uint8_t c) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(s); NOTUSED(c);
}

esp_err_t mdns_command(
    const char *c, const char *h, const char *s, const char *p, uint16_t t
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(c); NOTUSED(h); NOTUSED(s); NOTUSED(p); NOTUSED(t);
}

esp_err_t sntp_command(
    const char *c, const char *h, const char *m, uint32_t i
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(c); NOTUSED(h); NOTUSED(m); NOTUSED(i);
}

esp_err_t ping_command(
    const char *h, uint16_t i, uint16_t s, uint16_t c, bool a
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(h); NOTUSED(i); NOTUSED(s); NOTUSED(c); NOTUSED(a);
}

esp_err_t iperf_command(
    const char *h, uint16_t p, uint16_t l, uint8_t i, uint8_t t, bool u, bool a
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(h); NOTUSED(p); NOTUSED(l);
    NOTUSED(i); NOTUSED(t); NOTUSED(u); NOTUSED(a);
}

esp_err_t tsync_command(const char *h, uint16_t p, uint32_t t, bool a) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(h); NOTUSED(p); NOTUSED(t); NOTUSED(a);
}
#endif // CONFIG_BASE_USE_WIFI
