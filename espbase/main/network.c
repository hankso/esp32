/*
 * File: network.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-21 13:33:54
 */

#include "network.h"
#include "config.h"
#include "update.h"             // for ota_updation_url
#include "server.h"             // for CTYPE_XXX
#include "avitool.h"            // for CAMERA_XXX
#include "drivers.h"            // for SPI && GPIO
#include "filesys.h"            // for filesys_xxx
#include "timesync.h"           // for timesync_xxx

#ifndef CONFIG_BASE_USE_NET
void network_initialize() {};
esp_err_t network_command(const char *c, const char *s, const char *p, uint16_t t) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c); NOTUSED(s); NOTUSED(p); NOTUSED(t);
}
esp_err_t network_parse_host(const char *h, void *d) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(h); NOTUSED(d);
}
esp_err_t network_parse_addr(const char *h, uint16_t p, void *d) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(h); NOTUSED(p); NOTUSED(d);
}
#else // CONFIG_BASE_USE_NET

#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/netdb.h"
#include "lwip/stats.h"
#include "lwip/lwip_napt.h"
#include "esp_http_client.h"
#include "dhcpserver/dhcpserver.h"

#define STA_CONN_BIT    BIT0    // sta connected to ap
#define STA_STOP_BIT    BIT1    // manually disconnect from ap
#define STA_FAIL_BIT    BIT2    // failed to connect to ap
#define STA_SCAN_BIT    BIT3    // wait for wifi scan done
#define FTM_DONE_BIT    BIT4    // got success ftm response
#define FTM_FAIL_BIT    BIT5    // got failure ftm response
#define PCAP_STOP_BIT   BIT6
#define MQTT_CONN_BIT   BIT7
#define TSS_STOP_BIT    BIT9
#define TSC_STOP_BIT    BIT10
#define TS_STOPPED_BIT  BIT11
#define HBT_STOP_BIT    BIT12

#define UNCHANGED       -1
#define PROTOCOL        ( WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | \
                          WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR )

static const char *TAG = "Network";

/*
 * Network utilities
 */

static struct {
    esp_netif_t *ifp;
#define if_ap  netifs[0].ifp
#define if_sta netifs[1].ifp
#define if_eth netifs[2].ifp
#ifdef CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
    uint32_t speed[2], lastms[2];
    uint64_t total[2], lastsz[2];
#endif
} netifs[3];

static EventGroupHandle_t evtgrp = NULL;

static EventBits_t waitBits(EventBits_t bits, uint32_t ms) {
    // ClearOnExit = false, WaitForAllBits = false
    return evtgrp ? xEventGroupWaitBits(
        evtgrp, bits, pdFALSE, pdFALSE, TIMEOUT(ms)) & bits : 0;
}

static bool getBits(EventBits_t bits) {
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

static uint32_t network_local_ip(esp_netif_t *);

#ifdef IDF_TARGET_V4 // backport from IDF v5
static esp_netif_t * esp_netif_get_default_netif() { return NULL; }
static esp_err_t esp_netif_set_default_netif(esp_netif_t *ifp) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ifp);
}
static esp_err_t esp_netif_napt_enable(esp_netif_t *ifp) {
#   ifndef WITH_NAPT
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ifp);
#   else
    uint32_t addr = network_local_ip(ifp);
    if (!esp_netif_is_netif_up(ifp) || !addr) return ESP_ERR_INVALID_STATE;
    ip_napt_enable(addr, true);
    return ESP_OK;
#   endif
}
#endif

static esp_err_t network_ctrl_dhcp(
    esp_netif_t *ifp, esp_netif_dhcp_status_t *st, int enable
) {
    esp_err_t err = ESP_OK;
    esp_netif_dhcp_status_t status;
    esp_netif_flags_t flag = esp_netif_get_flags(ifp);
    if (flag & ESP_NETIF_DHCP_SERVER) {
        if (( err = esp_netif_dhcps_get_status(ifp, &status) )) return err;
        if (enable > 0 && status != ESP_NETIF_DHCP_STARTED) {
            err = esp_netif_dhcps_start(ifp);
        } else if (!enable && status != ESP_NETIF_DHCP_STOPPED) {
            err = esp_netif_dhcps_stop(ifp);
        }
    } else {
        if (( err = esp_netif_dhcpc_get_status(ifp, &status) )) return err;
        if (enable > 0 && status != ESP_NETIF_DHCP_STARTED) {
            err = esp_netif_dhcpc_start(ifp);
        } else if (!enable && status != ESP_NETIF_DHCP_STOPPED) {
            err = esp_netif_dhcpc_stop(ifp);
        }
    }
    if (!err && st) *st = status;
    return err;
}

#define BITS2NETMASK(n)                                                     \
        ({ uint32_t v = 0; LOOPN(i, n) { v |= BIT(31 - i); } v; })
#define NETMASK2BITS(v)                                                     \
        ({ int n = 0; LOOPN(i, 32) { if ((v) & BIT(i)) n++; } n; })

static esp_err_t network_ctrl_static(
    esp_netif_t *ifp, const char *ip, const char *gw, const char *nm
) {
    if (!ifp || (!ip && !gw && !nm)) return ESP_ERR_INVALID_ARG;
    uint8_t nmbit = 0xFF;
    esp_err_t err = ESP_OK;
    esp_netif_ip_info_t info;
    size_t ilen = strlen(ip ?: "");
    char ipv4[IP4ADDR_STRLEN_MAX + 4], desc[16];
    bool dhcps = esp_netif_get_flags(ifp) & ESP_NETIF_DHCP_SERVER;
    if (( err = esp_netif_get_ip_info(ifp, &info) )) return err;
    if (ilen) {
        if (strlcpy(ipv4, ip, sizeof(ipv4)) >= sizeof(ipv4)) return ESP_ERR_INVALID_ARG;
        char *netmask = strtok(ipv4, "/") ? strtok(NULL, "/") : NULL;
        if (!inet_aton(ipv4, &info.ip)) return ESP_ERR_INVALID_ARG;
        if (netmask && parse_u8(netmask, &nmbit) && (nmbit < 2 || nmbit > 32))
            return ESP_ERR_INVALID_ARG;
    } else if (ip) goto done;                       // empty string
    if (nm && nm[0]) {
        if (!inet_aton(nm, &info.netmask)) return ESP_ERR_INVALID_ARG;
        if (!ip_addr_netmask_valid(&info.netmask)) return ESP_ERR_INVALID_ARG;
    } else if (nmbit < 33) {
        info.netmask.addr = PP_HTONL(BITS2NETMASK(nmbit));
    } else if (nm || !info.netmask.addr) {
        info.netmask.addr = PP_HTONL(0xFFFFFF00);   // fallback to 255.255.255.0
    }
    if (gw && gw[0]) {
        strlcpy(ipv4, gw, sizeof(ipv4)); strtok(ipv4, "/");
        if (!inet_aton(ipv4, &info.gw)) return ESP_ERR_INVALID_ARG;
    } else if (dhcps) {
        info.gw.addr = info.ip.addr;
    } else if (gw || !info.gw.addr) {
        info.gw.addr = (info.ip.addr & info.netmask.addr) | PP_HTONL(1);
    }
    if (ilen) {                                     // store netmask with ipaddr
        ilen = snprintf(
            (char *)(ip = ipv4), sizeof(ipv4), "%s/%u",
            inet_ntoa(info.ip.addr), NETMASK2BITS(info.netmask.addr));
    }
    if (!err) err = network_ctrl_dhcp(ifp, NULL, false);
    if (!err) err = esp_netif_set_ip_info(ifp, &info);
done:
    strlcpy(desc, esp_netif_get_desc(ifp), sizeof(desc));
    strupr(desc);
#ifdef CONFIG_BASE_USE_ETH
    if (startswith(desc, "ETH")) {
        if (!err && ip) err = config_set("net.eth.host", ip);
        if (!err && gw) err = config_set("net.eth.gate", gw);
    }
#endif
#ifdef CONFIG_BASE_USE_WIFI
    if (!strcmp(desc, "STA")) {
        if (!err && ip) err = config_set("net.sta.host", ip);
        if (!err && gw) err = config_set("net.sta.gate", gw);
    }
    if (!strcmp(desc, "AP")) {
        if (!err && ip) err = config_set("net.ap.host", ip);
    }
#endif
    if (err || dhcps || (ip && !ilen)) network_ctrl_dhcp(ifp, NULL, true);
    if (err) {
        ESP_LOGE(TAG, "%s static IP failed: %s", desc, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "%s static IP updated", desc);
    }
    return err;
}

#if CONFIG_LWIP_IPV6
static const char * network_ipv6_tstr(esp_ip6_addr_t *ip) {
    switch (esp_netif_ip6_get_addr_type(ip)) {
    case ESP_IP6_ADDR_IS_GLOBAL:        return "global";
    case ESP_IP6_ADDR_IS_LINK_LOCAL:    return "link-local";
    case ESP_IP6_ADDR_IS_SITE_LOCAL:    return "site-local";
    case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:  return "unique-local";
    default: return NULL;
    }
}
#endif

static void network_print_ipinfo(esp_netif_t *ifp, FILE *stream) {
    const char *desc = esp_netif_get_desc(ifp), *tstr;
    const char *types[] = { "main", "backup", "fallback" };
    esp_netif_ip_info_t ip;
    esp_netif_dns_info_t dns;
    esp_netif_get_ip_info(ifp, &ip);
    if (stream) {
        fprintf(stream, "IP " IPSTR ", GW " IPSTR ", Mask " IPSTR "\n",
                IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
    } else {
        ESP_LOGI(TAG, "IF %s IP " IPSTR ", GW " IPSTR ", Mask " IPSTR,
                desc, IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
    }
#if CONFIG_LWIP_IPV6
    esp_ip6_addr_t ip6s[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
    LOOPN(i, esp_netif_get_all_ip6(ifp, ip6s)) {
        if (!( tstr = network_ipv6_tstr(ip6s + i) )) continue;
        if (stream) {
            fprintf(stream, "IP " IPV6STR " (%s)\n", IPV62STR(ip6s[i]), tstr);
        } else {
            ESP_LOGI(TAG, "IF %s IP " IPV6STR " (%s)", desc, IPV62STR(ip6s[i]), tstr);
        }
    }
#endif
    LOOPN(i, MIN(ESP_NETIF_DNS_MAX, LEN(types))) {
        if (!esp_netif_get_dns_info(ifp, i, &dns) && !ip_addr_isany(&dns.ip)) {
            if (stream) {
                fprintf(stream, "DNS %s " IPSTR "\n",
                        types[i], IP2STR(&dns.ip.u_addr.ip4));
            } else {
                ESP_LOGI(TAG, "IF %s DNS %s " IPSTR,
                        desc, types[i], IP2STR(&dns.ip.u_addr.ip4));
            }
        }
    }
}

static esp_err_t network_print_detail(const char *iface) {
    const char
        *tstr, *desc,
        *types[] = { "main", "backup" }, // FIXME: DNS "fallback"
        *dstrs[] = { "inited", "started", "stopped" },
        *fstrs[] = {
            "DHCPC", "DHCPS", "AUTOUP", "GARP", "IP MODIFIED",
            "PPP", "SLIP", "MLDV6", "IPV6 AUTO"
        };
    uint8_t mac[6];
    esp_netif_t *ifp = NULL, *dft = esp_netif_get_default_netif();
    esp_netif_ip_info_t ip;
    esp_netif_dns_info_t dns;
    esp_netif_dhcp_status_t status;
    int idx = 0, num = esp_netif_get_nr_of_ifs();
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 2, 0)
    while (( ifp = esp_netif_next(ifp) )) {
#else
    while (( ifp = esp_netif_next_unsafe(ifp) )) {
#endif
        desc = esp_netif_get_desc(ifp);
        if (iface && !strstr(desc, iface)) continue;
        if (network_ctrl_dhcp(ifp, &status, UNCHANGED)) status = 0;
        if (esp_netif_get_mac(ifp, mac)) memset(mac, 0xFF, sizeof(mac));
        if (esp_netif_get_ip_info(ifp, &ip)) memset(&ip, 0, sizeof(ip));
        bool up = esp_netif_is_netif_up(ifp);
        esp_netif_flags_t flag = esp_netif_get_flags(ifp);
        char dhcp = flag & ESP_NETIF_DHCP_SERVER ? 's' : 'c';
        if (idx && !iface) putchar('\n');
        printf("[%d/%d] %3s <%s", ++idx, num, desc, esp_netif_get_ifkey(ifp));
        LOOPN(i, LEN(fstrs)) { if (bitread(flag, i)) printf(",%s", fstrs[i]); }
        printf("> %s%s", up ? "UP" : "DOWN", ifp == dft ? " default" : "");
        printf(" dhcp%c-%s\n", dhcp, dstrs[status]);
        printf("    ether " MACSTR "\n", MAC2STR(mac));
        printf("    inet4 " IPSTR " gateway " IPSTR " netmask " IPSTR "\n",
                IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
#if CONFIG_LWIP_IPV6
        esp_ip6_addr_t ip6s[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
        LOOPN(i, esp_netif_get_all_ip6(ifp, ip6s)) {
            if (!( tstr = network_ipv6_tstr(ip6s + i) )) continue;
            printf("    inet6 " IPV6STR " scope %s\n", IPV62STR(ip6s[i]), tstr);
        }
#endif
        LOOPN(i, MIN(ESP_NETIF_DNS_MAX, LEN(types))) {
            if (esp_netif_get_dns_info(ifp, i, &dns) || ip_addr_isany(&dns.ip))
                continue;
            printf("    dnsip " IPSTR " type %s\n",
                    IP2STR(&dns.ip.u_addr.ip4), types[i]);
        }
#if CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
        uint32_t currms = MSFPO();
        LOOPN(i, LEN(netifs)) {
            if (netifs[i].ifp != ifp) continue;
            LOOPN(j, LEN(netifs[i].speed)) {
                if ((currms - netifs[i].lastms[j]) > 2000) netifs[i].speed[j] = 0;
            }
            printf("     send %s", format_size(netifs[i].total[ESP_NETIF_TX]));
            printf(" %sps\n", format_size(netifs[i].speed[ESP_NETIF_TX]));
            printf("     recv %s", format_size(netifs[i].total[ESP_NETIF_RX]));
            printf(" %sps\n", format_size(netifs[i].speed[ESP_NETIF_RX]));
            break;
        }
#endif
    }
    return ESP_OK;
}

/*
 * WIFI & ETH initialize
 */

#ifdef CONFIG_BASE_USE_ETH
#   include "esp_eth.h"

static struct {
    esp_eth_handle_t hdl;
    spi_host_device_t bus;
    esp_eth_netif_glue_handle_t glue;
} eth;

static esp_err_t network_eth_init() {
    const char *names[] = {
#ifdef CONFIG_BASE_ETH_SPI
        "ETH RST", "ETH INT", "ETH MOSI", "ETH MISO", "ETH SCLK", "ETH CS"
#else
        "ETH RST", NULL, NULL, NULL, "ETH MDC", "ETH MDIO"
#endif
    };
    int pins[LEN(names)];
#ifdef CONFIG_BASE_ETH_CUSTOM_PINS
    const char *str = CONFIG_BASE_ETH_CUSTOM_PINS;
#else
    const char *str = CONFIG_BASE_ETH_PINS;
#endif
    if (parse_pin(str, pins, LEN(pins), names) != LEN(pins)) {
        ESP_LOGE(TAG, "Invalid pins `%s` for Ethernet", str);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;
    esp_eth_mac_t *mac = NULL;
    esp_eth_phy_t *phy = NULL;
    eth_mac_config_t mac_conf = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_conf = ETH_PHY_DEFAULT_CONFIG();
    phy_conf.reset_gpio_num = pins[0];

#ifdef CONFIG_BASE_ETH_SPI
    spi_bus_config_t bus_conf = {
        .mosi_io_num   = pins[2],
        .miso_io_num   = pins[3],
        .sclk_io_num   = pins[4],
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
    };
    spi_device_interface_config_t dev_conf = {
        .command_bits   = 16,
        .address_bits   = 8,
        .mode           = 0b00,
        .clock_speed_hz = SPI_MASTER_FREQ_20M,
        .queue_size     = 20,
        .spics_io_num   = pins[5],
    };
    eth.bus = SPI2_HOST;
#   ifdef CONFIG_BASE_USE_SPI
    if (NUM_SPI == SPI2_HOST) eth.bus = SPI3_HOST;
#   endif
    if (( err = gpio_install_isr_service(0) ) == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "GPIO ISR already installed");
        err = ESP_OK;
    }
    if (!err) err = spi_bus_initialize(eth.bus, &bus_conf, SPI_DMA_CH_AUTO);
#   ifdef IDF_TARGET_V4
    spi_device_handle_t spi_dev;
    if (!err) err = spi_bus_add_device(eth.bus, &dev_conf, &spi_dev);
    eth_w5500_config_t mod_conf = ETH_W5500_DEFAULT_CONFIG(spi_dev);
#   else
    eth_w5500_config_t mod_conf = ETH_W5500_DEFAULT_CONFIG(eth.bus, &dev_conf);
#   endif
    mod_conf.int_gpio_num = pins[1];
    mac = esp_eth_mac_new_w5500(&mod_conf, &mac_conf);
    phy = esp_eth_phy_new_w5500(&phy_conf);
#else // CONFIG_BASE_ETH_EMAC
    return ESP_ERR_NOT_SUPPORTED;
#endif // CONFIG_BASE_ETH_SPI

    esp_eth_config_t eth_conf = ETH_DEFAULT_CONFIG(mac, phy);
    if (!err) err = esp_eth_driver_install(&eth_conf, &eth.hdl);
#ifdef CONFIG_BASE_ETH_SPI
    uint8_t mac_eth[6], mac_ext[6];
    if (!err) err = esp_read_mac(mac_eth, ESP_MAC_ETH);
    if (!err) err = esp_eth_ioctl(eth.hdl, ETH_CMD_G_MAC_ADDR, mac_ext);
    if (!err && memcmp(mac_eth, mac_ext, 6)) {
        if (!( err = esp_eth_ioctl(eth.hdl, ETH_CMD_S_MAC_ADDR, mac_eth) ))
            ESP_LOGI(TAG, "Change ETH MAC from " MACSTR " to " MACSTR,
                     MAC2STR(mac_ext), MAC2STR(mac_eth));
    }
#endif
    if (!err) {
        eth.glue = esp_eth_new_netif_glue(eth.hdl);
    } else {
        TRYNULL(eth.glue, esp_eth_del_netif_glue);
        TRYNULL(eth.hdl, esp_eth_driver_uninstall);
        TRYNULL(eth.bus, spi_bus_free);
        if (mac) mac->del(mac);
        if (phy) phy->del(phy);
    }
    return err;
}
#endif

#ifdef CONFIG_BASE_USE_WIFI
#   include "esp_wifi.h"
#   ifdef CONFIG_BASE_USE_SMARTCONFIG
#       include "esp_smartconfig.h"
#   endif
#   ifndef IDF_TARGET_V4
#       include "esp_wifi_ap_get_sta_list.h"
#   endif
#   define HAS_AP(m)    ( m == WIFI_MODE_APSTA || m == WIFI_MODE_AP )
#   define HAS_STA(m)   ( m == WIFI_MODE_APSTA || m == WIFI_MODE_STA )

static wifi_config_t config_ap, config_sta;

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

static void wifi_print_apinfo(wifi_ap_record_t *aps, int num) {
    if (!num) return;
    int maxlen = 10;
    LOOPN(i, num) {
#   ifdef CONFIG_BASE_AUTO_ALIGN
        size_t len = strlen((char *)aps[i].ssid), tmp = len;
        LOOPN(j, len) { if (aps[i].ssid[i] > 0x80) tmp += 1; }
        maxlen = MAX(maxlen, tmp);
#   else
        maxlen = 24;
#   endif
    }
    printf("SSID%*s", maxlen - 4 + 1, "");
    puts("MAC address       RSSI Mode WPS FTM Auth   Code Channel");
    LOOPN(i, num) {
        const char *ftm = "";
        if (aps[i].ftm_responder && aps[i].ftm_initiator) {
            ftm = "yes";
        } else if (aps[i].ftm_responder) {
            ftm = "REP";
        } else if (aps[i].ftm_initiator) {
            ftm = "REQ";
        }
        char *ssid = (char *)aps[i].ssid, *utf8 = NULL;
        LOOPN(j, strlen(ssid)) {
            if (ssid[i] < 0x7F) continue;
            if (( utf8 = gbk2str(ssid) )) ssid = utf8;
            break;
        }
        wifi_country_t *cinfo = &aps[i].country;
        printf("%-*s " MACSTR "  %-3d %c%c%c%c %3s %3s %-6s %c%c%c%c %-2d",
            maxlen, ssid, MAC2STR(aps[i].bssid), aps[i].rssi,
            aps[i].phy_11b ? 'b' : ' ', aps[i].phy_11g ? 'g' : ' ',
            aps[i].phy_11n ? 'n' : ' ', aps[i].phy_lr ? 'l' : 'h',
            aps[i].wps ? "yes" : "", ftm, wifi_authmode_str(aps[i].authmode),
            cinfo->cc[0] ?: ' ', cinfo->cc[1] ?: ' ',
            cinfo->cc[2] == ' ' ? '-' : cinfo->cc[2] ?: ' ',
            cinfo->policy == WIFI_COUNTRY_POLICY_AUTO ? 'A' : 'M', aps[i].primary);
        if (cinfo->nchan)
            printf(" %u~%u", cinfo->schan, cinfo->schan + cinfo->nchan);
        putchar('\n');
        TRYFREE(utf8);
    }
}

static esp_err_t wifi_update_napt(esp_netif_t *if_src) {
    dhcps_offer_t opt = OFFER_DNS;
    esp_netif_dns_info_t info;
    esp_netif_dhcp_option_mode_t op = ESP_NETIF_OP_SET;
    esp_netif_dhcp_option_id_t id = ESP_NETIF_DOMAIN_NAME_SERVER;
    esp_err_t err = esp_netif_is_netif_up(if_ap) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!err) err = esp_netif_get_dns_info(if_src, ESP_NETIF_DNS_MAIN, &info);
    if (!err) err = network_ctrl_dhcp(if_ap, NULL, false);
    if (!err) err = esp_netif_dhcps_option(if_ap, op, id, &opt, sizeof(opt));
    if (!err) err = esp_netif_set_dns_info(if_ap, ESP_NETIF_DNS_MAIN, &info);
    if (!err) err = network_ctrl_dhcp(if_ap, NULL, true);
    if (!err) err = esp_netif_napt_enable(if_ap);
    if (!err) err = pmap_control("on");
    return err;
}

static uint16_t s_nap = 0;
static wifi_ap_record_t *s_aps = NULL;

static esp_err_t wifi_get_ap_records() {
    // Must call this function after sta scan
    uint16_t nap;
    wifi_ap_record_t *aps = NULL;
    esp_err_t err = esp_wifi_scan_get_ap_num(&nap) ?: !nap;
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

static UNUSED esp_err_t wifi_find_ap_record(
    const char *ssid, uint8_t *bssid, bool scan, wifi_ap_record_t *record
) {
    bool found = false;
    esp_err_t err = ESP_OK;
    LOOPN(i, s_nap) {
        if ((ssid && !strcmp((char *)s_aps[i].ssid, ssid))
        || (bssid && !memcmp(s_aps[i].bssid, bssid, sizeof(s_aps[i].bssid)))) {
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

esp_err_t wifi_sta_start(const char *ssid, const char *pass, const char *ip) {
    // Arguments validation
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!ssid) {
        if (!strlen(Config.net.STA_SSID)) return err;
        ssid = Config.net.STA_SSID;
        pass = Config.net.STA_PASS;
    } else if (!pass) {
        pass = "";
    }
    if (!ip) ip = Config.net.STA_HOST;

    // WiFi mode validation
    if (( err = wifi_mode_switch(true, UNCHANGED, NULL) )) return err;
    if (getBits(STA_CONN_BIT)) {
        wifi_ap_record_t info;
        err = esp_wifi_sta_get_ap_info(&info);
        if (err == ESP_OK && !strcmp((char *)info.ssid, ssid))
            return err;                         // already connected to this AP
        if (err != ESP_ERR_WIFI_NOT_CONNECT)
            wifi_sta_stop(false);               // disconnect from current AP
    }

    // Configure static IP address
    const char *gw = ip == Config.net.STA_HOST ? Config.net.STA_GATE : NULL;
    if (( err = network_ctrl_static(if_sta, ip, gw, NULL) )) return err;

    // Connect to the specified AP
    wifi_sta_config_t *sta = &config_sta.sta;
    sta->sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    strlcpy((char *)sta->ssid, ssid, sizeof(sta->ssid));
    strlcpy((char *)sta->password, pass, sizeof(sta->password));
    if (( err = esp_wifi_set_config(WIFI_IF_STA, &config_sta) )) return err;
    return strlen(ssid) ? esp_wifi_connect() : ESP_OK;
}

esp_err_t wifi_sta_stop(bool down) {
    setBits(STA_STOP_BIT);
    esp_err_t err = esp_wifi_disconnect();
    if (!err && down) err = wifi_mode_switch(false, UNCHANGED, NULL);
    return err;
}

esp_err_t wifi_sta_scan(
    const char *ssid, uint8_t channel, uint16_t tout_ms, bool verbose
) {
    wifi_mode_t mode;
    wifi_scan_config_t cfg = {
        .ssid = (uint8_t *)ssid,
        .channel = channel,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = wifi_mode_switch(UNCHANGED, UNCHANGED, &mode);
    if (!err) err = HAS_STA(mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!err) err = esp_wifi_scan_stop();
    if (!err) err = esp_wifi_clear_ap_list();
    if (err) return err;
    if (!tout_ms) return esp_wifi_scan_start(&cfg, false);
    cfg.scan_time.active.min = CONS(tout_ms / 15, 10, 1400);
    cfg.scan_time.active.max = CONS(tout_ms / 10, 20, 1500);
    setBits(STA_SCAN_BIT);
    if (!( err = esp_wifi_scan_start(&cfg, true) )
     && !( err = wifi_get_ap_records() ) && verbose) {
        ESP_LOGI(TAG, "STA found %d AP", s_nap);
        wifi_print_apinfo(s_aps, s_nap);
    }
    return err;
}

esp_err_t wifi_sta_wait(uint16_t tout_ms) {
    EventBits_t bits = waitBits(STA_CONN_BIT | STA_STOP_BIT | STA_FAIL_BIT, tout_ms);
    if (!bits) return ESP_ERR_TIMEOUT;
    if (bits & STA_FAIL_BIT) return ESP_FAIL;
    if (bits & STA_CONN_BIT) return ESP_OK;
    ESP_LOGW(TAG, "STA manually stopped by wifi_sta_stop");
    return ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_sta_list_ap() {
    wifi_mode_t mode;
    wifi_mode_switch(UNCHANGED, UNCHANGED, &mode);
    printf("STA ");
    if (!HAS_STA(mode)) {
        puts("not enabled");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen((char *)config_sta.sta.ssid))
        printf("SSID `%s` ", (char *)config_sta.sta.ssid);
    if (getBits(STA_FAIL_BIT)) {
        puts("connect failed");
    } else if (getBits(STA_CONN_BIT)) {
        int8_t power = 0;
        wifi_ps_type_t psave = 0;
        esp_wifi_get_ps(&psave);
        esp_wifi_get_max_tx_power(&power);
        printf("connected power-save=%s power-tx=%ddBm\n",
               psave == WIFI_PS_MAX_MODEM ? "max" :
               psave == WIFI_PS_MIN_MODEM ? "min" : "none",
               power / 4);
        network_print_ipinfo(if_sta, stdout);
        wifi_ap_record_t info;
        if (s_nap) {
            wifi_print_apinfo(s_aps, s_nap);
        } else if (!esp_wifi_sta_get_ap_info(&info)) {
            wifi_print_apinfo(&info, 1);
        }
    } else {
        puts("disconnected");
    }
    return ESP_OK;
}

esp_err_t wifi_ap_start(const char *ssid, const char *pass, const char *ip) {
    // Arguments validation
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!ssid) {
        if (!strlen(Config.net.AP_SSID)) return err;
        ssid = Config.net.AP_SSID;
        pass = Config.net.AP_PASS;
    } else if (!pass) {
        pass = "";
    }
    if (!ip) ip = Config.net.AP_HOST;

    // WiFi mode validation
    if (( err = wifi_mode_switch(UNCHANGED, true, NULL) )) return err;

    // Configure gateway IP address
    if (( err = network_ctrl_static(if_ap, ip, ip, NULL) )) return err;

    // Config the specified AP
    uint8_t val;
    wifi_ap_config_t *ap = &config_ap.ap;
    size_t slen = ssid == Config.net.AP_SSID ? strlen(ssid) : 0;
    size_t ulen = strlen(Config.info.UID), blen = slen + ulen + 1;
    if (slen && ulen && blen < sizeof(ap->ssid)) {
        sprintf((char *)ap->ssid, "%s-%s", ssid, Config.info.UID);
    } else {
        strlcpy((char *)ap->ssid, ssid, sizeof(ap->ssid));
    }
    if (strlen(pass) < 8) {
        ap->authmode = WIFI_AUTH_OPEN;
        ap->password[0] = 0;
    } else {
        ap->authmode = WIFI_AUTH_WPA2_PSK;
        strlcpy((char *)ap->password, pass, sizeof(ap->password));
    }
    ap->ssid_len = strlen((char *)ap->ssid);
    ap->ssid_hidden = strtob(Config.net.AP_HIDE);
#   ifdef CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
    ap->ftm_responder = true;
#   endif
    if (parse_u8(Config.net.AP_CHAN, &val) && val < 15)  ap->channel = val;
    if (parse_u8(Config.net.AP_NCON, &val) && val < 254) ap->max_connection = val;
    if (!( err = esp_wifi_set_config(WIFI_IF_AP, &config_ap) )) {
        ESP_LOGI(TAG, "AP SSID `%s`, PASS `%s`, Channel %d, Hidden %d",
                ap->ssid, ap->password, ap->channel, ap->ssid_hidden);
        if (( err = esp_wifi_set_protocol(WIFI_IF_AP, PROTOCOL) ))
            ESP_LOGW(TAG, "AP set protocol error: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_ap_stop() { return wifi_mode_switch(UNCHANGED, false, NULL); }

esp_err_t wifi_ap_list_sta() {
    if (!esp_netif_is_netif_up(if_ap)) {
        puts("AP not enabled");
        return ESP_OK;
    }
    uint8_t proto = 0, primary = 0;
    wifi_country_t cinfo = { 0 };
    wifi_bandwidth_t band = WIFI_BW20;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_err_t err = esp_wifi_get_country(&cinfo);
    if (!err) err = esp_wifi_get_channel(&primary, &second);
    if (!err) err = esp_wifi_get_protocol(WIFI_IF_AP, &proto);
    if (!err) err = esp_wifi_get_bandwidth(WIFI_IF_AP, &band);
    if (err) ESP_LOGW(TAG, "Get WiFi info error: %s", esp_err_to_name(err));
    printf(
        "AP SSID=%.*s Country=%c%c%c%c Channel=%d(%u~%u) MaxTX=%ddBm\n"
        "Primary=%d Second=%s Protocol=%c%c%c%c Bandwidth=%s\n",
        config_ap.ap.ssid_len, config_ap.ap.ssid,
        cinfo.cc[0] ?: ' ', cinfo.cc[1] ?: ' ',
        cinfo.cc[2] == ' ' ? '-' : cinfo.cc[2] ?: ' ',
        cinfo.policy == WIFI_COUNTRY_POLICY_AUTO ? 'A' : 'M',
        config_ap.ap.channel, cinfo.schan, cinfo.schan + cinfo.nchan,
        cinfo.max_tx_power / 4, primary,
        second == WIFI_SECOND_CHAN_BELOW ? "below" :
        second == WIFI_SECOND_CHAN_ABOVE ? "above" : "HT20",
        proto & WIFI_PROTOCOL_11B ? 'b' : ' ',
        proto & WIFI_PROTOCOL_11G ? 'g' : ' ',
        proto & WIFI_PROTOCOL_11N ? 'n' : ' ',
        proto & WIFI_PROTOCOL_LR ? 'l' : ' ',
        band == WIFI_BW20 ? "20MHz" : band == WIFI_BW40 ? "40MHz" :
        band == WIFI_BW80 ? "80MHz" : band == WIFI_BW160 ? "160MHz" :
        band == WIFI_BW_HT20 ? "HT20" : band == WIFI_BW_HT40 ? "HT40" :
        band == WIFI_BW80_BW80 ? "80+80MHZ" : ""
    );
    network_print_ipinfo(if_ap, stdout);

#   ifdef IDF_TARGET_V4
#       define wifi_sta_mac_ip_list_t           esp_netif_sta_list_t
#       define esp_wifi_ap_get_sta_list_with_ip esp_netif_get_sta_list
#   endif
    wifi_sta_list_t sta_info_list;
    wifi_sta_mac_ip_list_t sta_macip_list;
    err = esp_wifi_ap_get_sta_list(&sta_info_list) ?:
          esp_wifi_ap_get_sta_list_with_ip(&sta_info_list, &sta_macip_list);
    if (err || !sta_info_list.num) {
        if (err) printf("Could not get sta list: %s\n", esp_err_to_name(err));
        return err;
    }

    printf("\nAID  IP address       MAC address       RSSI Mode Mesh\n");
    LOOPN(i, sta_info_list.num) {
        uint16_t aid;
        wifi_sta_info_t *info = sta_info_list.sta + i;
        if (( err = esp_wifi_ap_get_sta_aid(info->mac, &aid) )) {
            ESP_LOGD(TAG, "Get STA AID failed: %s", esp_err_to_name(err));
            continue;
        }
        printf("%-4d %-16s " MACSTR " %4d %c%c%c%c %s\n",
            aid, inet_ntoa(sta_macip_list.sta[i].ip), MAC2STR(info->mac), info->rssi,
            info->phy_11b ? 'b' : ' ', info->phy_11g ? 'g' : ' ',
            info->phy_11n ? 'n' : ' ', info->phy_lr ? 'l' : 'h',
            info->is_mesh_child ? "true" : "false");
    }
    return err;
}

esp_err_t ftm_respond(const char *ctrl, int16_t offset_cm) {
#   ifdef CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT
    if (!esp_netif_is_netif_up(if_ap)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    if (offset_cm && !( err = esp_wifi_ftm_resp_set_offset(offset_cm) )) {
        ESP_LOGI(TAG, "FTM responder set offset to %dcm", offset_cm);
    } else if (err) {
        ESP_LOGW(TAG, "FTM responder offset failed: %s", esp_err_to_name(err));
        return err;
    }
    if (ctrl && strtob(ctrl) != config_ap.ap.ftm_responder) {
        config_ap.ap.ftm_responder = !config_ap.ap.ftm_responder;
        if (( err = esp_wifi_set_config(WIFI_IF_AP, &config_ap) )) return err;
    }
    ESP_LOGI(TAG, "FTM responder %s",
            config_ap.ap.ftm_responder ? "enabled" : "disabled");
    return err;
#   else
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ctrl); NOTUSED(offset_cm);
#   endif
}

esp_err_t ftm_request(const char *ssid, uint8_t count) {
#   ifdef CONFIG_ESP_WIFI_FTM_INITIATOR_SUPPORT
    if (!esp_netif_is_netif_up(if_sta)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    wifi_ap_record_t info;
    if (ssid) {
        if (( err = wifi_find_ap_record(ssid, NULL, true, &info) ))
            return err;
    } else if (getBits(STA_CONN_BIT)) {
        if (( err = esp_wifi_sta_get_ap_info(&info) ))
            return err;
    } else {
        ESP_LOGE(TAG, "STA disconnected. Provide SSID to run FTM.");
        return ESP_ERR_INVALID_ARG;
    }
    if (!info.ftm_responder) {
        ESP_LOGE(TAG, "FTM not supported by `%s`", (char *)info.ssid);
        return ESP_ERR_INVALID_ARG;
    }
    wifi_ftm_initiator_cfg_t cfg = {
        .channel = info.primary,
        .frm_count = 32,
        .burst_period = 2,  // 200ms
#       ifndef IDF_TARGET_V6
        .use_get_report_api = true,
#       endif
    };
    memcpy(cfg.resp_mac, info.bssid, sizeof(cfg.resp_mac));
    if (!(count % 8) && (count <= 32 || count == 64)) cfg.frm_count = count;
    ESP_LOGI(TAG, "FTM " MACSTR " channel=%d count=%d period=%dms",
             MAC2STR(cfg.resp_mac), cfg.channel,
             cfg.frm_count, cfg.burst_period * 100);
    if (( err = esp_wifi_ftm_initiate_session(&cfg) )) return err;
    EventBits_t bits = waitBits(FTM_DONE_BIT | FTM_FAIL_BIT, 3000);
    clearBits(bits);
    if (bits & FTM_FAIL_BIT) return ESP_FAIL;
    if (bits & FTM_DONE_BIT) return ESP_OK;
    esp_wifi_ftm_end_session();
    return ESP_ERR_TIMEOUT;
#   else
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ssid); NOTUSED(count);
#   endif
}
#endif // CONFIG_BASE_USE_WIFI

static void update_control(void *force) {
#ifdef CONFIG_BASE_OTA_FETCH
    TaskHandle_t task = xTaskGetHandle("ota_fetch");
    if (xTaskGetCurrentTaskHandle() != task) {
        static bool once = false;
        if (!force && ( once = true )) return;
        TRYNULL(task, vTaskDelete);
        xTaskCreate(update_control, "ota_fetch", 4096, NULL, 1, &task);
        if (task) return;
        ESP_LOGE(TAG, "Failed to fetch OTA: %s", esp_err_to_name(ESP_ERR_NO_MEM));
    } else {
        ota_updation_url(NULL);
    }
#else
    return; NOTUSED(force);
#endif
}

static void cb_network(void *a, esp_event_base_t base, int32_t id, void *data) {
    if (base == IP_EVENT) {
        if (id == IP_EVENT_AP_STAIPASSIGNED) {
            ip_event_ap_staipassigned_t *evt = data;
            ESP_LOGI(TAG, "AP client %s assigned", inet_ntoa(evt->ip.addr));
        } else if (id == IP_EVENT_STA_LOST_IP) {
            if (esp_netif_is_netif_up(if_eth)) esp_netif_set_default_netif(if_eth);
        } else if (id == IP_EVENT_ETH_LOST_IP) {
            if (esp_netif_is_netif_up(if_sta)) esp_netif_set_default_netif(if_sta);
        } else if (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP) {
            ip_event_got_ip_t *evt = data;
            esp_netif_set_default_netif(evt->esp_netif);
            if (evt->ip_changed) network_print_ipinfo(evt->esp_netif, NULL);
            if (strtob(Config.app.MQTT_RUN)) mqtt_control("on");
            if (strtob(Config.app.SNTP_RUN)) sntp_control("sync");
            if (strtob(Config.app.HBT_AUTO)) hbeat_control("on");
            if (strtob(Config.app.OTA_AUTO)) update_control(NULL);
#ifdef CONFIG_BASE_USE_WIFI
            if (strtob(Config.net.AP_AUTO)) wifi_ap_stop();
            if (strtob(Config.net.AP_NAPT)) wifi_update_napt(evt->esp_netif);
            if (id == IP_EVENT_STA_GOT_IP) {
                setBits(STA_CONN_BIT);
                clearBits(STA_FAIL_BIT | STA_STOP_BIT);
            }
#endif
        } else if (id == IP_EVENT_GOT_IP6) {
            ip_event_got_ip6_t *evt = data;
            network_print_ipinfo(evt->esp_netif, NULL);
        } else if (id == IP_EVENT_AP_STAIPASSIGNED) {
            ip_event_ap_staipassigned_t *evt = data;
            ESP_LOGI(TAG, "AP client %s assigned", inet_ntoa(evt->ip.addr));
#ifdef CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
        } else if (id == IP_EVENT_TX_RX) {
            ip_event_tx_rx_t *evt = data;
            LOOPN(i, LEN(netifs)) {
                if (netifs[i].ifp != evt->esp_netif) continue;
                uint32_t dtms = MSFPO() - netifs[i].lastms[evt->dir];
                netifs[i].lastms[evt->dir] += dtms;
                netifs[i].total[evt->dir] += evt->len;
                if (dtms < 1000) break;
                if (dtms > 2000) {
                    netifs[i].speed[evt->dir] = evt->len;
                } else {
                    netifs[i].speed[evt->dir] = (
                        netifs[i].total[evt->dir] - netifs[i].lastsz[evt->dir]
                    ) * 1000 / dtms;
                }
                netifs[i].lastsz[evt->dir] = netifs[i].total[evt->dir];
                break;
            }
#endif
        }
#ifdef CONFIG_BASE_USE_SMARTCONFIG
    } else if (base == SC_EVENT) {
        if (id == SC_EVENT_GOT_SSID_PSWD) {
            smartconfig_event_got_ssid_pswd_t *evt = data;
            wifi_sta_start((char *)evt->ssid, (char *)evt->password, NULL);
        } else if (id == SC_EVENT_SEND_ACK_DONE) {
            ESP_LOGI(TAG, "SC done");
            esp_smartconfig_stop();
        }
#endif
#ifdef CONFIG_BASE_USE_WIFI
    } else if (base == WIFI_EVENT) {
        static int retry = 0;
        if (id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *evt = data;
            ESP_LOGI(TAG, "AP client " MACSTR " join, AID=%d, Mesh=%d",
                    MAC2STR(evt->mac), evt->aid, evt->is_mesh_child);
            if (strtob(Config.net.AP_NAPT)) LOOPN(i, LEN(netifs)) {
                if (netifs[i].ifp == if_ap) continue;
                if (!esp_netif_is_netif_up(netifs[i].ifp)) continue;
                wifi_update_napt(netifs[i].ifp); break;
            }
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *evt = data;
            ESP_LOGI(TAG, "AP client " MACSTR " leave, AID=%d, Mesh=%d",
                    MAC2STR(evt->mac), evt->aid, evt->is_mesh_child);
        } else if (id == WIFI_EVENT_AP_WRONG_PASSWORD) {
            wifi_event_ap_wrong_password_t *evt = data;
            ESP_LOGI(TAG, "AP client " MACSTR " wrong password", MAC2STR(evt->mac));
        } else if (id == WIFI_EVENT_STA_CONNECTED) {
            wifi_event_sta_connected_t *evt = data;
            ESP_LOGI(TAG, "STA connect `%s` success", evt->ssid);
            retry = 0;
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            clearBits(STA_CONN_BIT);
            wifi_event_sta_disconnected_t *evt = data;
            if (getBits(STA_STOP_BIT)) {
                ESP_LOGI(TAG, "STA disconnect from `%s`", evt->ssid);
                clearBits(STA_STOP_BIT);
            } else if (evt->reason != WIFI_REASON_NO_AP_FOUND && retry < 3) {
                ESP_LOGD(TAG, "STA connect `%s` retry %d", evt->ssid, retry);
                retry++;
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "STA connect `%s` failed %d", evt->ssid, evt->reason);
                retry = 0;
                setBits(STA_FAIL_BIT);
                if (strtob(Config.net.AP_AUTO)) wifi_ap_start(NULL, NULL, NULL);
#ifdef CONFIG_BASE_USE_SMARTCONFIG
                if (strtob(Config.net.SC_AUTO)) {
                    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
                    esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
                    esp_smartconfig_start(&cfg);
                }
#endif
            }
        } else if (id == WIFI_EVENT_STA_NEIGHBOR_REP) {
            wifi_event_neighbor_report_t *evt = data;
            LOOPN(i, evt->report_len) {
                ESP_LOGI(TAG, "STA neighbor[%d] report %d", i, evt->n_report[i]);
            }
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            if (getBits(STA_SCAN_BIT)) {
                clearBits(STA_SCAN_BIT);
            } else if (!wifi_get_ap_records()) {
                ESP_LOGI(TAG, "STA found %d AP", s_nap);
                wifi_print_apinfo(s_aps, s_nap);
            }
        } else if (id == WIFI_EVENT_FTM_REPORT) {
            wifi_event_ftm_report_t *evt = data;
            if (evt->status != FTM_STATUS_SUCCESS) {
                ESP_LOGW(TAG, "FTM " MACSTR " failed: %s",
                         MAC2STR(evt->peer_mac), wifi_ftm_str(evt->status));
                setBits(FTM_FAIL_BIT);
            } else {
                ESP_LOGI(TAG, "FTM " MACSTR " RTT %" PRIu32 "ns, DIST %" PRIu32 "cm",
                        MAC2STR(evt->peer_mac), evt->rtt_est, evt->dist_est);
                wifi_ftm_report_entry_t *ents = NULL;
                if (evt->ftm_report_num_entries &&
                    !ECALLOC(ents, evt->ftm_report_num_entries, sizeof(*ents)) &&
                    !esp_wifi_ftm_get_report(ents, evt->ftm_report_num_entries)
                ) {
                    puts("ID  RSSI RTT (ns)");
                LOOPN(i, evt->ftm_report_num_entries) {
                    printf("%-3d %-3d %7.3f\n",
                           ents[i].dlog_token, ents[i].rssi, ents[i].rtt / 1e3);
                }
                }
                TRYFREE(ents);
                setBits(FTM_DONE_BIT);
            }
        }
#endif
#ifdef CONFIG_BASE_USE_ETH
    } else if (base == ETH_EVENT) {
        if (id == ETHERNET_EVENT_CONNECTED) {
            ESP_LOGI(TAG, "ETH Link up");
            network_ctrl_static(if_eth, Config.net.ETH_HOST, Config.net.ETH_GATE, NULL);
        } else if (id == ETHERNET_EVENT_DISCONNECTED) {
            ESP_LOGI(TAG, "ETH Link down");
        } else if (id == ETHERNET_EVENT_START) {
            ESP_LOGI(TAG, "ETH started");
        } else if (id == ETHERNET_EVENT_STOP) {
            ESP_LOGI(TAG, "ETH stopped");
        }
#endif
    }
}

void network_initialize() {
    ESP_ERROR_CHECK( esp_netif_init() );
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK( err );

    const char * tags[] = {
        "mdns_mem", "iperf", "esp_netif_lwip", "esp_netif_handlers",
#ifdef CONFIG_BASE_USE_ETH
        "esp_eth.netif.netif_glue",
#endif
#ifdef CONFIG_BASE_USE_WIFI
        "wifi", "wifi_init", "net80211",
#endif
    };
    ITERV(tag, tags) { esp_log_level_set(tag, ESP_LOG_WARN); }

    evtgrp = xEventGroupCreate();
#ifdef CONFIG_BASE_USE_ETH
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    if_eth = esp_netif_new(&netif_config);
    ESP_ERROR_CHECK( network_eth_init() );
    ESP_ERROR_CHECK( esp_netif_attach(if_eth, eth.glue) );
    ESP_ERROR_CHECK( esp_eth_start(eth.hdl) );
    ESP_ERROR_CHECK( REGEVTS(ETH, cb_network, NULL, NULL) );
#endif
#ifdef CONFIG_BASE_USE_WIFI
    if_ap = esp_netif_create_default_wifi_ap();
    if_sta = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&init_config) );
    ESP_ERROR_CHECK( esp_wifi_get_config(WIFI_IF_AP, &config_ap) );
    ESP_ERROR_CHECK( esp_wifi_get_config(WIFI_IF_STA, &config_sta) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( REGEVTS(WIFI, cb_network, NULL, NULL) );
#endif
#ifdef CONFIG_BASE_USE_SMARTCONFIG
    ESP_ERROR_CHECK( REGEVTS(SC, cb_network, NULL, NULL) );
#endif
    ESP_ERROR_CHECK( REGEVTS(IP, cb_network, NULL, NULL) );

    if (strtob(Config.app.MDNS_RUN) && ( err = mdns_control("on") ))
        ESP_LOGE(TAG, "Failed to start mDNS: %s", esp_err_to_name(err));
    if (strtob(Config.app.SNTP_RUN) && ( err = sntp_control("on") ))
        ESP_LOGE(TAG, "Failed to start SNTP: %s", esp_err_to_name(err));

#ifdef CONFIG_BASE_USE_WIFI
    size_t slen = strlen((char *)config_sta.sta.ssid);
    const char *ssid = slen ? (char *)config_sta.sta.ssid : NULL;
    const char *pass = slen ? (char *)config_sta.sta.password : NULL;
    if (!( err = wifi_sta_start(ssid, pass, NULL) )) return;
    if (err != ESP_ERR_INVALID_ARG)
        ESP_LOGE(TAG, "Failed to start STA: %s", esp_err_to_name(err));
    if (!strtob(Config.net.AP_AUTO)) return;
    slen = strlen((char *)config_ap.ap.ssid);
    if (!startswith((char *)config_ap.ap.ssid, Config.info.NAME)) slen = 0;
    ssid = slen ? (char *)config_ap.ap.ssid : NULL;
    pass = slen ? (char *)config_ap.ap.password : NULL;
    if (( err = wifi_ap_start(ssid, pass, NULL) ))
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(err));
#endif
}

static esp_netif_t *find_netif(const char *iface) {
    esp_netif_t *ifp = NULL, *dft = esp_netif_get_default_netif();
    if (!strlen(iface ?: "")) return dft;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
    while (( ifp = esp_netif_next_unsafe(ifp) )) {
#else
    while (( ifp = esp_netif_next(ifp) )) {
#endif
        if (!strcmp(iface, esp_netif_get_desc(ifp))) break;
    }
    return ifp;
}

esp_err_t network_command(
    const char *itf, const char *cmd, const char *ssid,
    const char *pass, const char *ipv4, uint16_t tout_ms
) {
    if (!itf || !cmd) goto exit;
    esp_netif_ip_info_t ip;
    esp_netif_t *ifp = find_netif(itf);
    esp_err_t err = !ifp ? ESP_ERR_INVALID_ARG : esp_netif_get_ip_info(ifp, &ip);
    if (err) return err;
    err = ESP_ERR_NOT_SUPPORTED;
    if (!strcasecmp(cmd, "dft")) {
#ifndef IDF_TARGET_V4
        err = esp_netif_set_default_netif(ifp);
#endif
    } else if (!strcasecmp(cmd, "scan")) {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_sta) return wifi_sta_scan(ssid, 0, tout_ms, true);
#endif
    } else if (!strcasecmp(cmd, "on") || !strcasecmp(cmd, "up")) {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_ap) {
            err = wifi_ap_start(ssid, pass, ipv4);
        } else if (ifp == if_sta) {
            err = wifi_sta_start(ssid, pass, ipv4);
            if (!err && tout_ms) err = wifi_sta_wait(tout_ms);
        }
#endif
    } else if (!strcasecmp(cmd, "off") || !strcasecmp(cmd, "down")) {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_ap) {
            err = wifi_ap_stop();
        } else if (ifp == if_sta) {
            err = wifi_sta_stop(!strcasecmp(cmd, "down"));
        }
#endif
    } else if (!strcasecmp(cmd, "info") || !strcasecmp(cmd, "list")) {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_ap)  return wifi_ap_list_sta();
        if (ifp == if_sta) return wifi_sta_list_ap();
#endif
    } else if (!strcasecmp(cmd, "ip") || !strcasecmp(cmd, "static")) {
        if (!ipv4) {
            puts(inet_ntoa(ip.ip.addr));
            err = ESP_OK;
        } else {
        err = network_ctrl_static(ifp, ipv4, NULL, NULL);
        }
    } else if (!strcasecmp(cmd, "gw") || !strcasecmp(cmd, "gateway")) {
        if (!ipv4) {
            puts(inet_ntoa(ip.gw.addr));
            err = ESP_OK;
        } else {
        err = network_ctrl_static(ifp, NULL, ipv4, NULL);
        }
    } else if (!strcasecmp(cmd, "nm") || !strcasecmp(cmd, "netmask")) {
        if (!ipv4) {
            puts(inet_ntoa(ip.netmask.addr));
            err = ESP_OK;
        } else {
        err = network_ctrl_static(ifp, NULL, NULL, ipv4);
        }
    } else {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err) return err;
exit:
    return network_print_detail(itf);
}

esp_err_t network_parse_host(const char *host, void *ipaddr) {
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
        if (!ipaddr) {
            puts(ipaddr_ntoa(&tmp));
        } else if (ptr == res) {
            *(ip_addr_t *)ipaddr = tmp;
        }
    }
    freeaddrinfo(res);
    return ESP_OK;
}

esp_err_t network_parse_addr(const char *host, uint16_t port, void *sockaddr) {
    ip_addr_t tmp;
    esp_err_t err = network_parse_host(host, &tmp);
    if (!err) {
        struct sockaddr_in *p4 = sockaddr;
        struct sockaddr_in6 *p6 = sockaddr;
        if (IP_IS_V4(&tmp)) {
            p4->sin_len = sizeof(*p4);
            p4->sin_family = AF_INET;
            p4->sin_port = htons(port);
            inet_addr_from_ip4addr(&p4->sin_addr, ip_2_ip4(&tmp));
            memset(p4->sin_zero, 0, SIN_ZERO_LEN);
        } else {
            p6->sin6_len = sizeof(*p6);
            p6->sin6_family = AF_INET6;
            p6->sin6_port = htons(port);
            inet6_addr_from_ip6addr(&p6->sin6_addr, ip_2_ip6(&tmp));
            p6->sin6_scope_id = ip6_addr_zone(ip_2_ip6(&tmp));
        }
    }
    return err;
}

static uint32_t network_local_ip(esp_netif_t *ifp) {
    esp_netif_ip_info_t ip = { 0 };
    if (!ifp) ifp = esp_netif_get_default_netif();
    if (!ifp && esp_netif_is_netif_up(if_eth)) ifp = if_eth;
    if (!ifp && esp_netif_is_netif_up(if_sta)
             && getBits(STA_CONN_BIT)) ifp = if_sta;
    if (ifp) esp_netif_get_ip_info(ifp, &ip);
    return ip.ip.addr;
}

/*
 * Network applications
 */

#ifdef WITH_NAPT
static struct {
    struct {
        uint8_t proto;
        uint16_t sport;
        uint32_t daddr;
        uint16_t dport;
    } PACKED ents[IP_PORTMAP_MAX];
#   define PMAP_CTX_SIZE sizeof(pmap.ents)
    void *nvs;
} pmap;

esp_err_t pmap_command(
    const char *ctrl, const char *protocol,
    uint16_t sport, const char *dipv4, uint16_t dport
) {
    esp_err_t err = ESP_OK;
    if (!pmap.nvs && (
        ( err = config_nvs_open(&pmap.nvs, "pmap", false) ) ||
        ( err = pmap_control("load") )
    )) return err;
    if (ctrl) {
        uint8_t proto = IPPROTO_TCP;
        if (protocol) {
            if (!strcasecmp(protocol, "udp")) proto = IPPROTO_UDP;
            if (!strcasecmp(protocol, "both")) proto |= IPPROTO_UDP;
        }
        if (!strcasecmp(ctrl, "add")) {
            uint32_t daddr;
            if (!sport || !dport || !inet_aton(dipv4 ?: "", &daddr))
                return ESP_ERR_INVALID_ARG;
            ITERP(ent, pmap.ents) {
                if (ent->proto & proto && ent->sport == sport) {
                    ent->daddr = daddr;
                    ent->dport = dport;
                    if (!( proto &= ~ent->proto )) break;
                }
            }
            ITERP(ent, pmap.ents) {
                if (!proto || ent->proto) continue;
                ent->proto = proto & IPPROTO_TCP ?: IPPROTO_UDP;
                ent->sport = sport;
                ent->daddr = daddr;
                ent->dport = dport;
                if (!( proto &= ~ent->proto )) break;
            }
            if (proto) return ESP_ERR_NO_MEM;
            err = pmap_control("on") ?: pmap_control("dump");
        } else if (!strcasecmp(ctrl, "del")) {
            ITERP(ent, pmap.ents) {
                if (ent->proto & proto && ent->sport == sport) {
                    ip_portmap_remove(ent->proto, ent->sport);
                    ent->proto = 0;
                }
            }
            err = pmap_control("on") ?: pmap_control("dump");
        } else if (!strcasecmp(ctrl, "load")) {
            if (config_nvs_read(pmap.nvs, "ctx", &pmap, PMAP_CTX_SIZE) < 0) {
                memset(&pmap, 0, PMAP_CTX_SIZE);
                err = pmap_control("dump");
            }
        } else if (!strcasecmp(ctrl, "dump")) {
            err = config_nvs_write(pmap.nvs, "ctx", &pmap, PMAP_CTX_SIZE);
            err = err < 0 ? -err : ESP_OK;
        } else if (strtob(ctrl)) {
            uint32_t saddr = network_local_ip(NULL);
            ITERP(ent, pmap.ents) {
                if (!ent->proto) continue;
                ip_portmap_add(ent->proto, saddr, ent->sport, ent->daddr, ent->dport);
            }
        } else {
            ITERP(ent, pmap.ents) {
                if (!ent->proto) continue;
                ip_portmap_remove(ent->proto, ent->sport);
            }
        }
    } else {
        printf("PMap SPort DPort DAddr\n");
        ITERP(ent, pmap.ents) {
            if (!ent->proto) continue;
            uint32_t daddr = ent->daddr; // suppress -Waddress-of-packed-member
            printf("%4s %5u %5u %s\n",
                   ent->proto == IPPROTO_TCP ? "TCP" : "UDP",
                   ent->sport, ent->dport, inet_ntoa(daddr));
        }

    }
    return err;
}
#else // WITH_NAPT
esp_err_t pmap_command(
    const char *c, const char *p, uint16_t s, const char *a, uint16_t d
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(c); NOTUSED(p); NOTUSED(s); NOTUSED(a); NOTUSED(d);
}
#endif // WITH_NAPT

#ifdef WITH_PCAP
#   include "pcap.h"

typedef struct {
    void *buffer;
    size_t length;
    uint32_t seconds;
    uint32_t microseconds;
} pcap_pkt_t;

static struct {
    uint32_t npkt;
    TickType_t tout;
    QueueHandle_t queue;
    pcap_link_type_t type;
    pcap_file_handle_t hdl;
    filesys_path_t filename;
} pcap;

#   ifdef CONFIG_BASE_USE_WIFI
static void pcap_wifi_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    if (type == WIFI_PKT_MISC || pkt->rx_ctrl.rx_state) return;
    pcap_pkt_t record = {
        .length       = pkt->rx_ctrl.sig_len - 4, // Frame Check Sequence(FCS)
        .seconds      = pkt->rx_ctrl.timestamp / 1000000,
        .microseconds = pkt->rx_ctrl.timestamp % 1000000
    };
    if (EMALLOC(record.buffer, record.length)) return;
    memcpy(record.buffer, pkt->payload, record.length);
    if (!xQueueSend(pcap.queue, &record, pcap.tout)) free(record.buffer);
}
#   endif

#   ifdef CONFIG_BASE_USE_ETH
static esp_err_t pcap_eth_cb(
    esp_eth_handle_t hdl, uint8_t *buf, uint32_t len, void *arg
) {
    const struct timeval *tv = get_systime_us();
    pcap_pkt_t record = { buf, len, tv->tv_sec, tv->tv_usec };
    if (xQueueSend(pcap.queue, &record, pcap.tout) != pdTRUE)
        free(record.buffer);
    return ESP_OK;
}
#   endif

static void pcap_task(void *arg) {
    esp_err_t err = ESP_OK;
    pcap_config_t cfg = {
        .major_version = PCAP_DEFAULT_VERSION_MAJOR,
        .minor_version = PCAP_DEFAULT_VERSION_MINOR,
        .time_zone = 0x08,  // CST-8
    };
    if (!( cfg.fp = fopen(pcap.filename, "w+") )) goto exit;
    if (( err = pcap_new_session(&cfg, &pcap.hdl) )) fclose(cfg.fp);
    if (!err) err = pcap_write_header(pcap.hdl, pcap.type);
    if (err) goto exit;
    if (!( pcap.queue = xQueueCreate(10, sizeof(pcap_pkt_t)) )) {
        err = ESP_ERR_NO_MEM;
#   ifdef CONFIG_BASE_USE_WIFI
    } else if (pcap.type == PCAP_LINK_TYPE_802_11) {
        if (!err) err = esp_wifi_set_promiscuous_rx_cb(pcap_wifi_cb);
        if (!err) err = esp_wifi_set_promiscuous(true);
#   endif
#   ifdef CONFIG_BASE_USE_ETH
    } else if (pcap.type == PCAP_LINK_TYPE_ETHERNET) {
        bool enable = true;
        if (!err) err = esp_eth_update_input_path(eth.hdl, pcap_eth_cb, NULL);
        if (!err) err = esp_eth_ioctl(eth.hdl, ETH_CMD_S_PROMISCUOUS, &enable);
#   endif
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err) goto clean;
    pcap_pkt_t pkt;
    uint32_t cnt = 0;
    clearBits(PCAP_STOP_BIT);
    while (!getBits(PCAP_STOP_BIT)) {
        if (!xQueueReceive(pcap.queue, &pkt, pcap.tout)) continue;
        pcap_capture_packet(
            pcap.hdl, pkt.buffer, pkt.length, pkt.seconds, pkt.microseconds);
        TRYFREE(pkt.buffer);
        if (pcap.npkt && cnt++ > pcap.npkt) break;
    }
clean:
#   ifdef CONFIG_BASE_USE_ETH
    if (pcap.type == PCAP_LINK_TYPE_ETHERNET) {
        bool disable = false;
        esp_eth_ioctl(eth.hdl, ETH_CMD_S_PROMISCUOUS, &disable);
        esp_eth_update_input_path(eth.hdl, NULL, NULL);
    }
#   endif
#   ifdef CONFIG_BASE_USE_WIFI
    if (pcap.type == PCAP_LINK_TYPE_802_11) esp_wifi_set_promiscuous(false);
#   endif
    LOOPN(i, pcap.queue ? uxQueueMessagesWaiting(pcap.queue) : 0) {
        xQueueReceive(pcap.queue, &pkt, pcap.tout);
        TRYFREE(pkt.buffer);
    }
exit:
    TRYNULL(pcap.hdl, pcap_del_session);
    TRYNULL(pcap.queue, vQueueDelete);
    setBits(PCAP_STOP_BIT);
    vTaskDelete(NULL);
}

esp_err_t pcap_command(const char *ctrl, const char *itf, uint32_t npkt) {
    TaskHandle_t task = xTaskGetHandle("pcap");
    if (ctrl) {
        if (!strtob(ctrl)) {
            setBits(PCAP_STOP_BIT);
        } else if (!task) {
            if (startswith(itf, "wifi")) {
                pcap.type = PCAP_LINK_TYPE_802_11;
                fjoinr(pcap.filename, 2, Config.sys.DIR_DATA, "wifi.pcap");
            } else {
                pcap.type = PCAP_LINK_TYPE_ETHERNET;
                fjoinr(pcap.filename, 2, Config.sys.DIR_DATA, "eth.pcap");
            }
            if (npkt != UINT32_MAX) pcap.npkt = npkt;
            xTaskCreate(pcap_task, "pcap", 8192, NULL, 10, &task);
            if (!task) return ESP_ERR_NO_MEM;
            if (waitBits(PCAP_STOP_BIT, 100)) return ESP_FAIL;
        }
    } else {
        printf("PCap %srunning\n", task ? "" : "not ");
        if (task) pcap_print_summary(pcap.hdl, stdout);
    }
    return ESP_OK;
}
#else // WITH_PCAP
esp_err_t pcap_command(const char *c, const char *i, uint32_t n) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c); NOTUSED(i); NOTUSED(n);
}
#endif // WITH_PCAP

#ifdef WITH_MQTT
#   include "mqtt_client.h"
#   include "http_parser.h"

static struct {
    void *nvs; // {"url": "xxx", "cid": "xxx", "tpc": {filter: msgid, ...}}
    cJSON *ctx, *url, *cid, *tpc;
    esp_mqtt_client_handle_t hdl;
} mqtt;

static void cb_mqtt(void *a, esp_event_base_t base, int32_t id, void *data) {
    esp_mqtt_event_t *evt = data;
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        setBits(MQTT_CONN_BIT);
        ESP_LOGI(TAG, "MQTT connected to %s (session: %d)",
                 mqtt.url->valuestring, evt->session_present);
        for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
            if (item->valueint > 0) continue;
            int msgid = esp_mqtt_client_subscribe(evt->client, item->string, 1);
            if (msgid < 0) {
                ESP_LOGW(TAG, "MQTT subscribe to %s failed: %d", item->string, msgid);
            } else {
                cJSON_SetNumberValue(item, msgid);
            }
        }
        mqtt_control("dump");
        break;
    case MQTT_EVENT_DISCONNECTED: clearBits(MQTT_CONN_BIT); break;
    case MQTT_EVENT_PUBLISHED: FALLTH;
    case MQTT_EVENT_SUBSCRIBED: FALLTH;
    case MQTT_EVENT_UNSUBSCRIBED: {
        char buf[16] = { 0 }, *topic = buf;
        snprintf(buf, sizeof(buf), "msgid=%d", evt->msg_id);
        for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
            if (item->valueint != evt->msg_id) continue;
            topic = item->string; break;
        }
        if (id == MQTT_EVENT_PUBLISHED) {
            ESP_LOGI(TAG, "MQTT published %s", topic);
        } else if (id == MQTT_EVENT_UNSUBSCRIBED) {
            ESP_LOGI(TAG, "MQTT unsubscribed %s", topic);
            cJSON_DeleteItemFromObject(mqtt.tpc, topic);
        } else if (evt->error_handle->error_type == MQTT_ERROR_TYPE_NONE) {
            ESP_LOGI(TAG, "MQTT subscribed to %s", topic);
        } else if (evt->data_len) {
            ESP_LOGW(TAG, "MQTT subscribe to %s failed: %.*s", topic, evt->data_len, evt->data);
            cJSON_SetNumberValue(cJSON_GetObjectItem(mqtt.tpc, topic), 0);
        }
    }   break;
    case MQTT_EVENT_DATA:
        // FIXME: filter duplicate messages with qos=1
        // if (evt->qos == 1) ...
        ESP_LOGI(TAG, "MQTT msgid=%d qos=%d retain=%d off=%d/%d %.*s: %.*s",
                 evt->msg_id, evt->qos, evt->retain,
                 evt->current_data_offset, evt->total_data_len,
                 evt->topic_len, evt->topic, evt->data_len, evt->data);
        break;
    case MQTT_EVENT_ERROR:
        if (evt->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "MQTT connection failed: %d",
                     evt->error_handle->connect_return_code);
        } else if (evt->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "MQTT transport error: tls=%d sock=%s",
                     evt->error_handle->esp_tls_last_esp_err,
                     strerror(evt->error_handle->esp_transport_sock_errno));
        }
        break;
    }
}

esp_err_t mqtt_command(
    const char *ctrl, const char *url, const char *topic, const mqtt_pub_t *pub
) {
    esp_err_t err = ESP_OK;
    if (!mqtt.nvs && (
        ( err = config_nvs_open(&mqtt.nvs, "mqtt", false) ) ||
        ( err = mqtt_control("load") )
    )) return err;
    if (url || (!mqtt.hdl && ctrl && strcasecmp(ctrl, "load") && strcasecmp(ctrl, "dump"))) {
        url = url ?: mqtt.url->valuestring;
        struct http_parser_url purl;
        http_parser_url_init(&purl);
        if (http_parser_parse_url(url, strlen(url), 0, &purl))
            return ESP_ERR_INVALID_ARG;
        esp_mqtt_client_config_t cfg = { .broker.address.uri = url };
        if (purl.field_data[UF_FRAGMENT].len) {
            char buf[256]; snprintf(buf, sizeof(buf), "%.*s",
                purl.field_data[UF_FRAGMENT].len, url + purl.field_data[UF_FRAGMENT].off);
            if (!cJSON_SetValuestring(mqtt.cid, buf)) return ESP_ERR_NO_MEM;
            cfg.credentials.client_id = mqtt.cid->valuestring;
        } else if (purl.field_data[UF_USERINFO].len) {
            cfg.credentials.set_null_client_id = true; // username override cid
        } else {
            cfg.credentials.client_id = NULL; // use default platform cid
        }
        if (mqtt.hdl) {
            bool connected = getBits(MQTT_CONN_BIT);
            if (connected) mqtt_control("off");
            err = esp_mqtt_set_config(mqtt.hdl, &cfg);
            if (connected) mqtt_control("on");
        } else if (!( mqtt.hdl = esp_mqtt_client_init(&cfg) )) {
            err = ESP_FAIL;
        } else {
            err = esp_mqtt_client_register_event(
                mqtt.hdl, ESP_EVENT_ANY_ID, cb_mqtt, NULL);
        }
        if (!err) cJSON_SetValuestring(mqtt.url, url);
        if (err || !ctrl) return err;
    }
    if (ctrl) {
        int msgid;
        if (!strcasecmp(ctrl, "unsub")) {
            if (!topic) return ESP_ERR_INVALID_ARG;
            for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
                if (strcasecmp(item->string, topic)) continue;
                if (item->valueint <= 0) return ESP_OK;
                if (( msgid = esp_mqtt_client_unsubscribe(mqtt.hdl, topic) ) < 0) {
                    err = msgid == -2 ? ESP_ERR_NO_MEM : ESP_FAIL;
                } else {
                    cJSON_SetNumberValue(item, msgid);
                }
                break;
            }
        } else if (!strcasecmp(ctrl, "sub")) {
            if (!topic) return ESP_ERR_INVALID_ARG;
            cJSON *found = NULL;
            for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
                if (strcasecmp(item->string, topic)) continue;
                if (item->valueint > 0) return ESP_OK;
                found = item; break;
            }
            if (( msgid = esp_mqtt_client_subscribe(mqtt.hdl, topic, 2) ) < 0) {
                err = msgid == -2 ? ESP_ERR_NO_MEM : ESP_FAIL;
            } else if (found) {
                cJSON_SetNumberValue(found, msgid);
            } else {
                cJSON_AddNumberToObject(mqtt.tpc, topic, msgid);
            }
        } else if (!strcasecmp(ctrl, "pub")) {
            if (!pub || !pub->data || !pub->len) return ESP_ERR_INVALID_ARG;
            if (!topic) {
                if (cJSON_GetArraySize(mqtt.tpc) != 1) return ESP_ERR_INVALID_ARG;
                topic = mqtt.tpc->child->string; // use first topic
            }
            if (( msgid = esp_mqtt_client_publish(
                mqtt.hdl, topic, pub->data, pub->len, pub->qos, pub->retain
            ) ) < 0) {
                err = msgid == -2 ? ESP_ERR_NO_MEM : ESP_FAIL;
            } else {
                ESP_LOGI(TAG, "MQTT publish %zu to %s msgid=%d", pub->len, topic, msgid);
            }
        } else if (!strcasecmp(ctrl, "load")) {
            cJSON *ctx = config_nvs_jload(mqtt.nvs, "ctx"), *uri, *cid, *tpc;
            if (cJSON_IsObject(ctx) &&
                ( uri = cJSON_GetObjectItem(ctx, "url") ) &&
                ( cid = cJSON_GetObjectItem(ctx, "cid") ) &&
                ( tpc = cJSON_GetObjectItem(ctx, "tpc") )
            ) {
                mqtt.ctx = ctx; mqtt.url = uri; mqtt.cid = cid; mqtt.tpc = tpc;
                return ESP_OK;
            }
            cJSON_Delete(ctx);
            if (( ctx = cJSON_CreateObject() ) &&
                ( uri = cJSON_AddStringToObject(ctx, "url", Config.app.MQTT_URL) ) &&
                ( cid = cJSON_AddStringToObject(ctx, "cid", "") ) &&
                ( tpc = cJSON_AddObjectToObject(ctx, "tpc") )
            ) {
                mqtt.ctx = ctx; mqtt.url = uri; mqtt.cid = cid; mqtt.tpc = tpc;
                return mqtt_control("dump");
            }
            cJSON_Delete(ctx);
            err = ESP_ERR_NO_MEM;
        } else if (!strcasecmp(ctrl, "dump")) {
            cJSON *ctx = cJSON_Duplicate(mqtt.ctx, true),
                  *tpc = cJSON_GetObjectItem(ctx, "tpc");
            if (!ctx) return mqtt.ctx ? ESP_ERR_INVALID_STATE : ESP_ERR_NO_MEM;
            for (cJSON *item = tpc->child, *next; item; item = next) {
                next = item->next;
                if (item->valueint > 0) {   // subscribed
                    cJSON_SetNumberValue(item, 0);
                } else {                    // unsubscribed
                    cJSON_DeleteItemFromObject(tpc, item->string);
                }
            }
            err = config_nvs_jdump(mqtt.nvs, "ctx", ctx);
            err = err < 0 ? -err : ESP_OK;
            cJSON_Delete(ctx);
        } else if (!strtob(ctrl)) {
            if (!( err = esp_mqtt_client_stop(mqtt.hdl) )) {
                clearBits(MQTT_CONN_BIT);
                for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
                    cJSON_SetNumberValue(item, 0);
                }
            }
        } else if (!getBits(MQTT_CONN_BIT)) {
            err = esp_mqtt_client_start(mqtt.hdl);
            waitBits(MQTT_CONN_BIT, 3000);
        }
    } else if (!mqtt.hdl || !mqtt.ctx) {
        printf("MQTT not %s\n", mqtt.hdl ? "loaded" : "inited");
    } else {
        printf("MQTT %sconnected to %s\n",
               getBits(MQTT_CONN_BIT) ? "" : "not ", mqtt.url->valuestring);
        for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
            printf(" - %5d: %s\n", item->valueint, item->string);
        }
    }
    return ESP_OK;
}
#else // WITH_MQTT
esp_err_t mqtt_command(const char *c, const char *u, const char *t, const mqtt_pub_t *p) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c); NOTUSED(u); NOTUSED(t); NOTUSED(p);
}
#endif // WITH_MQTT

#ifdef WITH_MDNS
#   include "mdns.h"
#   include "lwip/apps/netbiosns.h"

static void mdns_print_results(mdns_result_t *r) {
    for (int i = 1; r; i++, r = r->next) {
        printf("%d: Interface: %s TTL %" PRIu32 " IPv%c\n",
#   ifdef IDF_TARGET_V4
                i,        r->tcpip_if == MDNS_IF_AP
                ? "AP"  : r->tcpip_if == MDNS_IF_STA
                ? "STA" : r->tcpip_if == MDNS_IF_ETH
                ? "ETH" : "Unknown",
#   else
                i, esp_netif_get_desc(r->esp_netif),
#   endif
                r->ttl, r->ip_protocol ? '6' : '4');
        if (r->instance_name)
            printf("  PTR : %s.%s.%s\n",
                   r->instance_name, r->service_type, r->proto);
        if (r->hostname)
            printf("  SRV : %s.local:%u\n", r->hostname, r->port);
        if (r->txt_count) {
            printf("  TXT : [%zu] ", r->txt_count);
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
    ESP_LOGI(TAG, "Query A/AAAA: %s.local", hostname);
    struct esp_ip4_addr ip4 = { .addr = 0 };
    struct esp_ip6_addr ip6 = { .addr = 0 };
    esp_err_t err4 = mdns_query_a(hostname, tout_ms, &ip4);
    esp_err_t err6 = mdns_query_aaaa(hostname, tout_ms, &ip6);
    if (err4 == ESP_ERR_NOT_FOUND && err6 == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Host not found: %s", hostname);
        return ESP_FAIL;
    }
    if (err4) {
        ESP_LOGW(TAG, "Query A failed: %s", esp_err_to_name(err4));
    } else {
        ESP_LOGI(TAG, "Found %s A at " IPSTR, hostname, IP2STR(&ip4));
    }
    if (err6) {
        ESP_LOGW(TAG, "Query AAAA failed: %s", esp_err_to_name(err6));
    } else {
        ESP_LOGI(TAG, "Found %s AAAA at " IPV6STR, hostname, IPV62STR(ip6));
    }
    return ESP_OK;
}

static esp_err_t mdns_initialize() {
    char hostname[MDNS_NAME_MAX_LEN];
    snprintf(hostname, sizeof(hostname), "%s-%s",
             strlen(Config.app.MDNS_HOST)
             ? Config.app.MDNS_HOST
             : Config.info.NAME,
             Config.info.UID);
    esp_err_t err = mdns_init();
    if (!err) err = mdns_hostname_set(hostname);
    if (!err) {
        mdns_txt_item_t desc[] = {
            { "name", Config.info.NAME },
            { "ver", Config.info.VER },
            { "uid", Config.info.UID }
        };
        mdns_service_add(NULL, "_id", "_tcp", 1, desc, LEN(desc));
        mdns_service_add(NULL, "_tss", "_tcp", TIMESYNC_PORT, NULL, 0);
#   ifdef CONFIG_BASE_USE_WEBSERVER
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
#   endif
        ESP_LOGI(TAG, "mDNS hostname set to %s", hostname);
        netbiosns_init();
#   ifndef NETBIOS_LWIP_NAME
        hostname[15] = '\0';    // must be less than 15 characters
        netbiosns_set_name(hostname);
        ESP_LOGI(TAG, "NetBIOS hostname set to %s", hostname);
#   endif
    } else {
        netbiosns_stop();
        mdns_free();
    }
    return err;
}

esp_err_t mdns_command(
    const char *ctrl, const char *hostname,
    const char *serv, const char *prot, uint16_t tout_ms
) {
    static bool running = false;
    if (ctrl) {
        if (!strtob(ctrl)) {
            netbiosns_stop();
            mdns_free();
            running = false;
        } else if (!running) {
            esp_err_t err = mdns_initialize();
            running = !err;
            if (err) return err;
        }
    } else if (hostname) {
        return mdns_query_host(hostname, tout_ms ?: 2000);
    } else if (serv || prot) {
        char sbuf[32] = "_http", pbuf[32] = "_tcp";
        if (serv) snprintf(sbuf, 32, "%s%s", serv[0] == '_' ? "" : "_", serv);
        if (prot) snprintf(pbuf, 32, "%s%s", prot[0] == '_' ? "" : "_", prot);
        return mdns_query_service(sbuf, pbuf, tout_ms ?: 3000);
    } else {
        printf("mDNS %srunning\n", running ? "" : "not ");
    }
    return ESP_OK;
}
#else // WITH_MDNS
esp_err_t mdns_command(
    const char *c, const char *h, const char *s, const char *p, uint16_t t
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(c); NOTUSED(h); NOTUSED(s); NOTUSED(p); NOTUSED(t);
}
#endif // WITH_MDNS

#include "esp_sntp.h"

static void sntp_notification_cb(struct timeval *tv) {
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
    err = network_parse_host(host, &addr);
    if (getBits(STA_CONN_BIT) && err) return err;
    bool updated = false;
#if SNTP_SERVER_DNS
    if (!str_like_ipaddr(host)) {
        esp_sntp_setservername(idx, host);
        updated = true;
    } else
#endif
    if (!err) {
        esp_sntp_setserver(idx, &addr);
        updated = true;
    }
#if SNTP_GET_SERVERS_FROM_DHCP || SNTP_GET_SERVERS_FROM_DHCPV6
    if (updated) {
        esp_sntp_servermode_dhcp(0);
    } else if (ip_addr_isany(esp_sntp_getserver(idx))) {
        esp_sntp_servermode_dhcp(1);
    }
#endif
    if (updated) ESP_LOGI(TAG, "SNTP server#%d set to %s", idx, host);
    return err;
}

esp_err_t sntp_command(
    const char *ctrl, const char *host, const char *mode, uint32_t intv_ms
) {
#ifdef IDF_TARGET_V4
#   define esp_sntp_getoperatingmode    sntp_getoperatingmode
#   define esp_sntp_getreachability     sntp_getreachability
#endif
    bool enabled = esp_sntp_enabled();
    if (ctrl) {
        if (!strcasecmp(ctrl, "reset") || !strcasecmp(ctrl, "sync")) {
            esp_sntp_restart();
        } else if (!strtob(ctrl)) {
            esp_sntp_stop();
        } else if (!enabled) {
            esp_sntp_set_time_sync_notification_cb(sntp_notification_cb);
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setserverhost(0, Config.app.SNTP_HOST);
            esp_sntp_init();
        }
    } else if (host) {
        int idx = SNTP_MAX_SERVERS - 1;
        LOOPN(i, SNTP_MAX_SERVERS) {
            if (ip_addr_isany(esp_sntp_getserver(i))) idx = i;
        }
        sntp_setserverhost(idx, host);
    } else if (mode) {
        if (!strcasecmp(mode, "smooth")) {
            esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
        } else {
            esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        }
    } else if (intv_ms) {
        if (intv_ms != esp_sntp_get_sync_interval())
            esp_sntp_set_sync_interval(intv_ms);
    } else {
        printf("SNTP %srunning\n", enabled ? "" : "not ");
        if (!enabled) return ESP_OK;
        printf(
            "Status:   %s\n"
            "SyncMode: %s\n"
            "OperMode: %s\n"
            "Interval: %" PRIu32 "s\n"
            "Datetime: %s\n",
            sntp_status_str(esp_sntp_get_sync_status()),
            esp_sntp_get_sync_mode() ? "smooth" : "immediate",
            esp_sntp_getoperatingmode() ? "listen only" : "poll",
            esp_sntp_get_sync_interval() / 1000,
            format_datetime_us(NULL));
        LOOPN(i, SNTP_MAX_SERVERS) {
            const ip_addr_t *addr = esp_sntp_getserver(i);
            if (ip_addr_isany(addr)) continue;
            printf("Server#%d: %s", i, ipaddr_ntoa(addr));
#if SNTP_SERVER_DNS
            const char *name = esp_sntp_getservername(i);
            if (name) printf(" (%s)", name);
#endif
#if SNTP_MONITOR_SERVER_REACHABILITY
            if (!esp_sntp_getreachability(i)) printf(" (unreachable)");
#endif
            putchar('\n');
        }
    }
    return ESP_OK;
}

#include "ping/ping_sock.h"

#define GET_PING_PROF(name, var) \
    esp_ping_get_profile(hdl, ESP_PING_PROF_ ## name, &(var), sizeof(var))

static void ping_command_success(esp_ping_handle_t hdl, void *arg) {
    uint8_t ttl;
    uint16_t seqno;
    uint32_t dtms, size;
    ip_addr_t target;
    GET_PING_PROF(TTL, ttl);
    GET_PING_PROF(SIZE, size);
    GET_PING_PROF(SEQNO, seqno);
    GET_PING_PROF(TIMEGAP, dtms);
    GET_PING_PROF(IPADDR, target);
    printf("%" PRIu32 " bytes from %s icmp_seq=%u ttl=%u time=%" PRIu32 "ms\n",
           size, ipaddr_ntoa(&target), seqno, ttl, dtms);
}

static void ping_command_timeout(esp_ping_handle_t hdl, void *arg) {
    uint16_t seqno;
    ip_addr_t target;
    GET_PING_PROF(SEQNO, seqno);
    GET_PING_PROF(IPADDR, target);
    printf("From %s: icmp_seq=%u timeout\n", ipaddr_ntoa(&target), seqno);
}

static void ping_command_end(esp_ping_handle_t hdl, void *ptr) {
    ip_addr_t target;
    uint32_t sent, recv, dtms;
    GET_PING_PROF(REPLY, recv);
    GET_PING_PROF(REQUEST, sent);
    GET_PING_PROF(DURATION, dtms);
    GET_PING_PROF(IPADDR, target);
    uint8_t lost_pcnt = 100 * (sent - recv) / (sent ?: 1);
    printf("Ping %s: %" PRIu32 " sent, %" PRIu32 " recv, "
           "%" PRIu32 " lost (%u%%) in %" PRIu32 "ms\n",
           ipaddr_ntoa(&target), sent, recv, sent - recv, lost_pcnt, dtms);
    esp_ping_delete_session(hdl);
    if (ptr) *(esp_ping_handle_t *)ptr = NULL;
}

#undef GET_PING_PROF

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

    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    if (network_parse_host(host, &cfg.target_addr)) {
        printf("Invalid host to ping: %s\n", host);
        return ESP_ERR_INVALID_ARG;
    }
    if (intv)   cfg.interval_ms = intv;
    if (size)   cfg.data_size = size;
    if (count)  cfg.count = count;
    cfg.task_stack_size = 4096;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_command_success,
        .on_ping_timeout = ping_command_timeout,
        .on_ping_end = ping_command_end,
        .cb_args = &hdl
    };
    esp_err_t err = esp_ping_new_session(&cfg, &cbs, &hdl);
    if (!err) err = esp_ping_start(hdl);
    if (err) {
        TRYNULL(hdl, esp_ping_delete_session);
    } else if (intv && cfg.count != ESP_PING_COUNT_INFINITE) {
        msleep(intv * cfg.count + 10);
    }
    return err;
}

#ifdef WITH_IPERF
#   include "iperf.h"

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
    uint32_t src_ip = network_local_ip(NULL);
    uint32_t dst_ip = ipaddr_addr(host ?: "");
    esp_err_t err = src_ip ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!err && host && dst_ip == IPADDR_NONE) err = ESP_ERR_INVALID_ARG;
    if (err) return err;

    iperf_cfg_t cfg = {
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
    if (cfg.time < cfg.interval) cfg.time = cfg.interval;
    if (!( err = iperf_start(&cfg) )) {
        char sip[IP4ADDR_STRLEN_MAX], dip[IP4ADDR_STRLEN_MAX];
        ESP_LOGI(TAG,
            "IPerf %s-%s src=%s:%u, dst=%s:%u, intv=%" PRIu32 ", tout=%" PRIu32,
            udp ? "udp" : "tcp", host ? "client" : "server",
            inet_ntoa_r(cfg.source_ip4, sip, sizeof(sip)), cfg.sport,
            inet_ntoa_r(cfg.destination_ip4, dip, sizeof(dip)), cfg.dport,
            cfg.interval, cfg.time);
    }
    return err;
}
#else // WITH_IPERF
esp_err_t iperf_command(
    const char *h, uint16_t p, uint16_t l,
    uint8_t i, uint8_t t, bool u, bool a
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(h); NOTUSED(p); NOTUSED(l); NOTUSED(i);
    NOTUSED(t); NOTUSED(u); NOTUSED(a);
}
#endif // WITH_IPERF

typedef struct {
    const char *host;
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
        // do NOT use vTaskDelete because we need clean on exit
        setBits(TSS_STOP_BIT | TSC_STOP_BIT);
        if (waitBits(TS_STOPPED_BIT, tout_ms ?: 1000)) {
            if (server) puts("TimeSync server stopped");
            if (client) puts("TimeSync client stopped");
        }
    } else if (server || client) {
        printf("TimeSync %s running\n", server ? "server" : "client");
    } else if (host && !strlen(host)) {
        puts("TimeSync not running");
    } else {
        timesync_param_t param = {
            .host = host,
            .port = port ?: TIMESYNC_PORT,
            .tout = tout_ms ?: (host ? 30000 : 500), // client 30s, server 0.5s
        };
        TaskHandle_t task = NULL;
        clearBits(TS_STOPPED_BIT);
        if (host) {
            xTaskCreate(timesync_client_task, "tsc", 1024, &param, 5, &task);
        } else {
            xTaskCreate(timesync_server_task, "tss", 2048, &param, 5, &task);
        }
        if (!task) return ESP_ERR_NO_MEM;
        if (waitBits(TS_STOPPED_BIT, 100)) return ESP_FAIL;
    }
    return ESP_OK;
}

static struct {
    float hbtime;
    float intval;
    char apssid[32];
    char appass[32];
    char hbturl[128];
    char imgurl[128];
    char location[256];
    char deviceid[256];
#   define HBEAT_CTX_SIZE sizeof(hbeat) - sizeof(void *)
    void *nvs;
} hbeat;

static void hbeat_update(cJSON *obj) {
    bool changed = false;
    for (cJSON *ptr = obj->child; ptr; ptr = ptr->next) {
        if (!ptr->string) continue;
        const char *key = ptr->string;
        int slen = cJSON_IsString(ptr) ? strlen(ptr->valuestring) : -1;
        if ((!strcmp(key, "deviceid") || !strcmp(key, "device_id")) && slen >= 0) {
            strlcpy(hbeat.deviceid, ptr->valuestring, sizeof(hbeat.deviceid));
        } else if (!strcmp(key, "location") && slen >= 0) {
            strlcpy(hbeat.location, ptr->valuestring, sizeof(hbeat.location));
        } else if (!strcmp(key, "apssid") && slen > 0) {
            if (strcmp(hbeat.apssid, ptr->valuestring)) changed = true;
            strlcpy(hbeat.apssid, ptr->valuestring, sizeof(hbeat.apssid));
        } else if (!strcmp(key, "appass") && slen > 8) {
            if (strcmp(hbeat.appass, ptr->valuestring)) changed = true;
            strlcpy(hbeat.appass, ptr->valuestring, sizeof(hbeat.appass));
        } else if (!strcmp(key, "hbtime") || !strcmp(key, "heartbeat_time")) {
            if (cJSON_GetNumberValue(ptr) > 0) hbeat.hbtime = ptr->valuedouble;
        } else if (!strcmp(key, "intval") || !strcmp(key, "capture_gap")) {
            if (cJSON_GetNumberValue(ptr) >= 0) hbeat.intval = ptr->valuedouble;
        } else if (!strcmp(key, "hurl") && slen > 7) {
            strlcpy(hbeat.hbturl, ptr->valuestring, sizeof(hbeat.hbturl));
        } else if (!strcmp(key, "iurl") && slen > 7) {
            strlcpy(hbeat.imgurl, ptr->valuestring, sizeof(hbeat.imgurl));
        }
    }
#ifdef CONFIG_BASE_USE_WIFI
    if (changed) {
        if (strlen(hbeat.apssid)) {
            wifi_ap_start(hbeat.apssid, hbeat.appass, NULL);
        } else {
            wifi_ap_stop();
        }
    }
#endif
    hbeat_control("dump");
}

static void hbeat_task(void *arg) {
#ifdef IDF_TARGET_V4
#   define CLEN_FORMAT PRId32
#else
#   define CLEN_FORMAT PRId64
#endif
    esp_err_t err = ESP_OK;
    esp_http_client_handle_t client = NULL;
    uint32_t addr = 0, blen = 256, rlen = 2048;
    char mac[9], macstr[32], *body = NULL, *resp = NULL;
    cJSON *info = cJSON_CreateObject(), *rst = NULL, *ts = NULL, *ip = NULL;
    if (( err = EMALLOC(body, blen) )
     || ( err = EMALLOC(resp, rlen) )
     || ( err = esp_base_mac_addr_get((uint8_t *)mac) )
    ) goto error;
    snprintf(macstr, sizeof(macstr), MACSTR, MAC2STR(mac));
    cJSON_AddStringToObject(info, "sn", Config.info.UID);
    cJSON_AddStringToObject(info, "mac_address", macstr);
    ts = cJSON_AddStringToObject(info, "beat_time", "XXXX-XX-XX XX:XX:XX");
    ip = cJSON_AddStringToObject(info, "ip_address", "XXX.XXX.XXX.XXX");
    if (!info || !ts || !ip) {
        err = ESP_ERR_NO_MEM;
        goto error;
    }
    if (!strlen(hbeat.hbturl)) {
        if (!strlen(Config.app.HBT_URL)) {
            err = ESP_ERR_INVALID_ARG;
            goto error;
        }
        ESP_LOGW(TAG, "HBT URL fallback to %s", Config.app.HBT_URL);
        strlcpy(hbeat.hbturl, Config.app.HBT_URL, sizeof(hbeat.hbturl));
    }
    esp_http_client_config_t cfg = {
        .url = hbeat.hbturl,
        .method = HTTP_METHOD_POST
    };
    if (!( client = esp_http_client_init(&cfg) )) {
        err = ESP_FAIL;
        goto error;
    }
    clearBits(HBT_STOP_BIT);
    TickType_t last[2] = { 0, 0 };
    while (hbeat.hbtime > 0 && !getBits(HBT_STOP_BIT)) {
        waitBits(HBT_STOP_BIT, 500);
        if (!( addr = network_local_ip(NULL) )) continue;
        TickType_t curr = xTaskGetTickCount(),
                   dhbt = TIMEOUT((int)(hbeat.hbtime * 1000)),
                   ditv = TIMEOUT((int)(hbeat.intval * 1000));
        if (!last[0] || (curr - last[0]) >= dhbt) {
            last[0] = curr;
            time_t sec = get_timestamp_us(NULL);
            inet_ntoa_r(addr, ip->valuestring, 16);
            strftime(ts->valuestring, 20, "%F %T", localtime(&sec));
            char *json = cJSON_PrintUnformatted(info);
            if (!json) {
                ESP_LOGE(TAG, "HBT generate info failed");
                continue;
            }
            memset(resp, 0, rlen);
            esp_http_client_set_url(client, hbeat.hbturl);
            esp_http_client_set_header(client, "Content-Type", CTYPE_JSON);
            esp_http_client_set_timeout_ms(client, 3000);
            if (( err = esp_http_client_open(client, strlen(json)) )) {
                ESP_LOGE(TAG, "HBT post request: %s", esp_err_to_name(err));
            } else if (
                   esp_http_client_write(client, json, strlen(json)) < 0
                || esp_http_client_fetch_headers(client) < 0
                || esp_http_client_read_response(client, resp, rlen - 1) < 0
            ) {
                ESP_LOGE(TAG, "HBT fetch response failed");
            } else if (!( rst = cJSON_Parse(resp) )) {
                ESP_LOGE(TAG, "HBT parse reponse: %s", cJSON_GetErrorPtr());
            } else {
                ESP_LOGI(TAG, "HBT %s status %d, resp %" CLEN_FORMAT,
                        hbeat.hbturl, esp_http_client_get_status_code(client),
                        esp_http_client_get_content_length(client));
                hbeat_update(rst);
                puts(resp);
            }
            esp_http_client_close(client);
            TRYNULL(rst, cJSON_Delete);
            TRYFREE(json);
        }
        if (!strlen(hbeat.imgurl) || hbeat.intval <= 0) continue;
        if (!last[1] || (curr - last[1]) >= ditv) {
            last[1] = curr;
            video_data_t frame;
            if (CAMERA_FETCH(frame)) {
                ESP_LOGE(TAG, "HBT generate data failed");
                continue;
            }
            memset(resp, 0, rlen);
            snprintf(body, blen,
                "--FRAME\r\nContent-Disposition:form-data;"
                    "name=\"ip_address\"\r\n\r\n%s\r\n"
                "--FRAME\r\nContent-Disposition:form-data;"
                    "name=\"mac_address\"\r\n\r\n%s\r\n"
                "--FRAME\r\nContent-Disposition:form-data;"
                    "name=\"image\"; filename=\"image\"\r\n\r\n",
                inet_ntoa(addr), macstr);
            size_t blen = strlen(body) + frame.len + 13;
            esp_http_client_set_url(client, hbeat.imgurl);
            esp_http_client_set_header(client, "Content-Type", CTYPE_BDRY(FRAME));
            esp_http_client_set_timeout_ms(client, 10000);
            if (( err = esp_http_client_open(client, blen) )) {
                ESP_LOGE(TAG, "HBT post request: %s", esp_err_to_name(err));
            } else if (
                   esp_http_client_write(client, body, strlen(body)) < 0
                || esp_http_client_write(client, frame.data, frame.len) < 0
                || esp_http_client_write(client, "\r\n--FRAME--\r\n", 13) < 0
                || esp_http_client_fetch_headers(client) < 0
                || esp_http_client_read_response(client, resp, rlen - 1) < 0
            ) {
                ESP_LOGE(TAG, "HBT fetch response failed");
            } else {
                ESP_LOGI(TAG, "HBT %s status %d, dlen %zu resp %" CLEN_FORMAT,
                        hbeat.imgurl, esp_http_client_get_status_code(client),
                        frame.len, esp_http_client_get_content_length(client));
                if (strlen(resp)) {
                    char *buf = resp, *utf8 = NULL;
                    LOOPN(i, strlen(buf)) {
                        if (buf[i] < 0x7F) continue;
                        if (( utf8 = gbk2str(buf) )) buf = utf8;
                        break;
                    }
                    puts(buf);
                    TRYFREE(utf8);
                }
            }
            esp_http_client_close(client);
            CAMERA_YIELD(frame);
        }
    }
    goto exit;
error:
    ESP_LOGE(TAG, "Failed to start HBT: %s", esp_err_to_name(err));
exit:
    TRYNULL(client, esp_http_client_cleanup);
    TRYNULL(info, cJSON_Delete);
    TRYNULL(rst, cJSON_Delete);
    TRYFREE(resp); TRYFREE(body);
    setBits(HBT_STOP_BIT);
    vTaskDelete(NULL);
}

esp_err_t hbeat_command(
    const char *ctrl, const char *hurl, const char *iurl, float hbtime, float intval
) {
    esp_err_t err = ESP_OK;
    if (!hbeat.nvs && (
        ( err = config_nvs_open(&hbeat.nvs, "hbeat", false) ) ||
        ( err = hbeat_control("load") )
    )) return err;
    TaskHandle_t task = xTaskGetHandle("hbeat");
    if (ctrl) {
        if (!strcasecmp(ctrl, "load")) {
        if (config_nvs_read(hbeat.nvs, "ctx", &hbeat, HBEAT_CTX_SIZE) < 0) {
            memset(&hbeat, 0, HBEAT_CTX_SIZE);
            strlcpy(hbeat.hbturl, Config.app.HBT_URL, sizeof(hbeat.hbturl));
            hbeat.hbtime = 30;
            hbeat.intval = 5;
                err = hbeat_control("dump");
            }
        } else if (!strcasecmp(ctrl, "dump")) {
            err = config_nvs_write(hbeat.nvs, "ctx", &hbeat, HBEAT_CTX_SIZE);
            err = err < 0 ? -err : ESP_OK;
        } else if (!strtob(ctrl)) {
            setBits(HBT_STOP_BIT);
        } else if (!task) {
            xTaskCreate(hbeat_task, "hbeat", 4096, NULL, 10, &task);
            err = !task ? ESP_ERR_NO_MEM : waitBits(HBT_STOP_BIT, 100) ? ESP_FAIL : ESP_OK;
        }
    } else if (hurl || iurl || hbtime != -1 || intval != -1) {
        if (strlen(hurl ?: "") > 7)
            strlcpy(hbeat.hbturl, hurl, sizeof(hbeat.hbturl));
        if (strlen(iurl ?: "") > 7)
            strlcpy(hbeat.imgurl, iurl, sizeof(hbeat.imgurl));
        if (hbtime > 0) hbeat.hbtime = hbtime;
        if (intval >= 0) hbeat.intval = intval;
        err = hbeat_control("dump");
    } else {
        printf("HeartBeat %srunning\n", task ? "" : "not ");
        char *buf = hbeat.location, *utf8 = NULL;
        LOOPN(i, strlen(buf)) {
            if (buf[i] < 0x7F) continue;
            if (( utf8 = gbk2str(buf) )) buf = utf8;
            break;
        }
        printf(
            " - hbturl  : %s\n"
            " - imgurl  : %s\n"
            " - hbtime  : %.3f\n"
            " - intval  : %.3f\n"
            " - apssid  : %s\n"
            " - appass  : %s\n"
            " - deviceid: %s\n"
            " - location: %s\n",
            hbeat.hbturl, hbeat.imgurl,
            hbeat.hbtime, hbeat.intval,
            hbeat.apssid, hbeat.appass,
            hbeat.deviceid, buf
        );
        TRYFREE(utf8);
    }
    return err;
}
#endif // CONFIG_BASE_USE_NET
