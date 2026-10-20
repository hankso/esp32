/*
 * File: network.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-21 13:33:54
 */

#include "network.h"
#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "update.h"             // for ota_updation_url
#include "server.h"             // for CTYPE_XXX
#include "avitool.h"            // for CAMERA_XXX
#include "console.h"            // for console_xxx
#include "drivers.h"            // for SPI && GPIO
#include "filesys.h"            // for filesys_xxx

#ifndef CONFIG_BASE_USE_NET
void network_initialize() {};
esp_err_t network_command(
    const char *i, const char *c, const char *s,
    const char *p, const char *h, uint16_t t
) {
    return ESP_ERR_NOT_SUPPORTED;
    NOTUSED(i); NOTUSED(c); NOTUSED(s); NOTUSED(p); NOTUSED(h); NOTUSED(t);
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
#define STA_FAIL_BIT    BIT1    // failed to connect to ap
#define STA_STOP_BIT    BIT2    // manually disconnect from ap
#define STA_WAIT_BIT    BIT3    // wait for sta state change
#define STA_SCAN_BIT    BIT4    // wait for wifi scan done
#define NOW_STOP_BIT    BIT5    // esp-now queue task stop
#define FTM_DONE_BIT    BIT6    // got success ftm response
#define FTM_FAIL_BIT    BIT7    // got failure ftm response
#define PCAP_STOP_BIT   BIT8    // pcap message task stop
#define MQTT_CONN_BIT   BIT9    // mqtt connected to broker
#define TSS_STOP_BIT    BIT10    // timesync server stopped
#define TSC_STOP_BIT    BIT11   // timesync client stopped
#define TS_STOPPED_BIT  BIT12   // timesync service stopped
#define HBT_STOP_BIT    BIT13   // heartbeat service stopped

#define UNCHANGED       -1

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
#define setbits(b)      ( evtgrp ? xEventGroupSetBits(evtgrp, (b)) : false )
#define getbits(b)      ( evtgrp ? xEventGroupGetBits(evtgrp) & (b) : false )
#define clrbits(b)      ( evtgrp ? xEventGroupClearBits(evtgrp, (b)) : false )
#define waitbits(b, m)  ( evtgrp ? xEventGroupWaitBits( /* Clear=0, WaitAll=0 */ \
                          evtgrp, (b), pdFALSE, pdFALSE, TIMEOUT(m)) & (b) : 0 )

static esp_err_t network_parse_ipaddr(const char *host, ip_addr_t *addr) {
    // Currently LWIP ip_addr_t is equivalent to ESP-NETIF esp_ip_addr_t in IDF v5.4
    struct sockaddr_storage sa;
    if (parse_address(host, NULL, &sa)) return ESP_ERR_INVALID_ARG;
    if (sa.ss_family == AF_INET) {
        addr->type = IPADDR_TYPE_V4;
        inet_addr_to_ip4addr(ip_2_ip4(addr), &((struct sockaddr_in *)&sa)->sin_addr);
    } else if (sa.ss_family == AF_INET6) {
        addr->type = IPADDR_TYPE_V6;
        inet6_addr_to_ip6addr(ip_2_ip6(addr), &((struct sockaddr_in6 *)&sa)->sin6_addr);
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static int network_local_ipaddr(esp_netif_t *ifp, int type, esp_ip_addr_t *addr) {
    // Get requested type of ip address from specified or default interface
    // Return ESP_IPADDR_TYPE_V4/6 or -1 if failed
    if (!ifp) ifp = esp_netif_get_default_netif();
    if (!ifp && esp_netif_is_netif_up(if_eth)) ifp = if_eth;
    if (!ifp && esp_netif_is_netif_up(if_sta)
             && getbits(STA_CONN_BIT)) ifp = if_sta;
    if (ifp) {
        esp_netif_ip_info_t info = { 0 };
        if ((type == ESP_IPADDR_TYPE_ANY || type == ESP_IPADDR_TYPE_V4) &&
            !esp_netif_get_ip_info(ifp, &info) && info.ip.addr
        ) {
            addr->u_addr.ip4.addr = info.ip.addr;
            return addr->type = ESP_IPADDR_TYPE_V4;
        }
        if ((type == ESP_IPADDR_TYPE_ANY || type == ESP_IPADDR_TYPE_V6) &&
            !esp_netif_get_ip6_global(ifp, &addr->u_addr.ip6)
        ) return addr->type = ESP_IPADDR_TYPE_V6;
    }
    return -1;
}

static uint32_t network_local_ipv4(esp_netif_t *ifp, esp_ip4_addr_t *ip4) {
    esp_ip_addr_t ipaddr = { 0 };
    if (network_local_ipaddr(ifp, ESP_IPADDR_TYPE_V4, &ipaddr) < 0) return 0;
    if (ip4) ip4->addr = ipaddr.u_addr.ip4.addr;
    return ipaddr.u_addr.ip4.addr;
}

static uint8_t *network_local_ipv6(esp_netif_t *ifp, esp_ip6_addr_t *ip6) {
    static esp_ip_addr_t ipaddr;
    if (network_local_ipaddr(ifp, ESP_IPADDR_TYPE_V6, &ipaddr) < 0) return NULL;
    if (ip6) memcpy(ip6->addr, ipaddr.u_addr.ip6.addr, sizeof(ip6->addr));
    return (uint8_t *)ipaddr.u_addr.ip6.addr;
}

static const char *network_local_ipstr(esp_netif_t *ifp, char *dst, size_t dstlen) {
    static char buf[ADDRSTRLEN];
    if (!dst) { dst = buf; dstlen = sizeof(buf); }
    esp_ip_addr_t ipaddr;
    switch (network_local_ipaddr(ifp, ESP_IPADDR_TYPE_ANY, &ipaddr)) {
    case ESP_IPADDR_TYPE_V4:
        snprintf(dst, dstlen, IPSTR, IP2STR(&ipaddr.u_addr.ip4)); break;
    case ESP_IPADDR_TYPE_V6:
        snprintf(dst, dstlen, IPV6STR, IPV62STR(ipaddr.u_addr.ip6)); break;
    default:
        return NULL;
    }
    return dst;
}

#ifdef IDF_TARGET_V4 // backport from IDF v5
static esp_netif_t * esp_netif_get_default_netif() { return NULL; }
static esp_err_t esp_netif_set_default_netif(esp_netif_t *ifp) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ifp);
}
static esp_err_t esp_netif_napt_enable(esp_netif_t *ifp) {
#   ifndef WITH_NAPT
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(ifp);
#   else
    uint32_t ipv4 = network_local_ipv4(ifp, NULL);
    if (!esp_netif_is_netif_up(ifp) || !ipv4) return ESP_ERR_INVALID_STATE;
    ip_napt_enable(ipv4, true);
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
    char ipv4[ADDRSTRLEN], desc[16];
    bool dhcps = esp_netif_get_flags(ifp) & ESP_NETIF_DHCP_SERVER;
    if (( err = esp_netif_get_ip_info(ifp, &info) )) return err;
    if (ilen) {
        if (strlcpy(ipv4, ip, sizeof(ipv4)) >= sizeof(ipv4)) return ESP_ERR_INVALID_ARG;
        char *netmask = strtok(ipv4, "/") ? strtok(NULL, "/") : NULL;
        if (( err = esp_netif_str_to_ip4(ipv4, &info.ip) )) return err;
        if (netmask && parse_u8(netmask, &nmbit) && (nmbit < 2 || nmbit > 32))
            return ESP_ERR_INVALID_ARG;
    } else if (ip) goto done;                       // empty string
    if (nm && nm[0]) {
        if (( err = esp_netif_str_to_ip4(nm, &info.netmask) )) return err;
        if (!ip4_addr_netmask_valid(info.netmask.addr)) return ESP_ERR_INVALID_ARG;
    } else if (nmbit < 33) {
        info.netmask.addr = PP_HTONL(BITS2NETMASK(nmbit));
    } else if (nm || !info.netmask.addr) {
        info.netmask.addr = PP_HTONL(0xFFFFFF00);   // fallback to 255.255.255.0
    }
    if (gw && gw[0]) {
        strlcpy(ipv4, gw, sizeof(ipv4)); strtok(ipv4, "/");
        if (( err = esp_netif_str_to_ip4(ipv4, &info.gw) )) return err;
    } else if (dhcps) {
        info.gw.addr = info.ip.addr;
    } else if (gw || !info.gw.addr) {
        info.gw.addr = (info.ip.addr & info.netmask.addr) | PP_HTONL(1);
    }
    if (ilen) {                                     // store netmask with ipaddr
        ilen = snprintf(
            (char *)(ip = ipv4), sizeof(ipv4), IPSTR "/%u",
            IP2STR(&info.ip), NETMASK2BITS(info.netmask.addr));
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

static const char * network_ipv6_tstr(esp_ip6_addr_t *ip) {
    switch (esp_netif_ip6_get_addr_type(ip)) {
    case ESP_IP6_ADDR_IS_GLOBAL:            return "global";
    case ESP_IP6_ADDR_IS_LINK_LOCAL:        return "link-local";
    case ESP_IP6_ADDR_IS_SITE_LOCAL:        return "site-local";
    case ESP_IP6_ADDR_IS_UNIQUE_LOCAL:      return "unique-local";
    case ESP_IP6_ADDR_IS_IPV4_MAPPED_IPV6:  return "ipv4-mapped";
    default:                                return "unknown";
    }
}

static void network_print_ipinfo(esp_netif_t *ifp) {
    esp_netif_ip_info_t ip;
    esp_netif_dns_info_t dns;
    if (!esp_netif_get_ip_info(ifp, &ip))
        printf("IP " IPSTR ", GW " IPSTR ", Mask " IPSTR "\n",
               IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
    esp_ip6_addr_t ip6s[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
    LOOPN(i, esp_netif_get_all_ip6(ifp, ip6s)) {
        printf("IP[%d] " IPV6STR " (%s)\n",
               i, IPV62STR(ip6s[i]), network_ipv6_tstr(ip6s + i));
    }
    const char *types[] = { "main", "backup", "fallback" };
    LOOPN(i, MIN(ifp == if_ap ? 1 : ESP_NETIF_DNS_MAX, LEN(types))) {
        if (!esp_netif_get_dns_info(ifp, i, &dns) && !ESP_IP_IS_ANY(dns.ip))
            printf("DNS %s " IPSTR "\n", types[i], IP2STR(&dns.ip.u_addr.ip4));
    }
}

static esp_err_t network_print_detail(const char *iface) {
    const char
        *tstr, *desc,
        *types[] = { "main", "backup", "fallback" },
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
        bool up = esp_netif_is_netif_up(ifp);
        esp_netif_flags_t flag = esp_netif_get_flags(ifp);
        char dhcp = flag & ESP_NETIF_DHCP_SERVER ? 's' : 'c';
        if (idx && !iface) putchar('\n');
        printf("[%d/%d] %3s <%s", ++idx, num, desc, esp_netif_get_ifkey(ifp));
        LOOPN(i, LEN(fstrs)) { if (bitread(flag, i)) printf(",%s", fstrs[i]); }
        printf("> %s%s", up ? "UP" : "DOWN", ifp == dft ? " default" : "");
        printf(" dhcp%c-%s\n", dhcp, dstrs[status]);
        printf("    ether " MACSTR "\n", MAC2STR(mac));
        if (!esp_netif_get_ip_info(ifp, &ip))
            printf("    inet4 " IPSTR " gateway " IPSTR " netmask " IPSTR "\n",
                   IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&ip.netmask));
        esp_ip6_addr_t ip6s[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
        LOOPN(i, esp_netif_get_all_ip6(ifp, ip6s)) {
            if (!( tstr = network_ipv6_tstr(ip6s + i) )) continue;
            printf("    inet6 " IPV6STR " scope %s\n", IPV62STR(ip6s[i]), tstr);
        }
        LOOPN(i, MIN(ifp == if_ap ? 1 : ESP_NETIF_DNS_MAX, LEN(types))) {
            if (esp_netif_get_dns_info(ifp, i, &dns) || ESP_IP_IS_ANY(dns.ip)) continue;
            printf("    dnsip " IPSTR " type %s\n", IP2STR(&dns.ip.u_addr.ip4), types[i]);
        }
#if CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
        uint32_t currms = MSFPO();
        LOOPN(i, LEN(netifs)) {
            if (netifs[i].ifp != ifp) continue;
            LOOPN(j, LEN(netifs[i].speed)) {
                if ((currms - netifs[i].lastms[j]) > 2000) netifs[i].speed[j] = 0;
            }
            printf("     send %9s", format_size(netifs[i].total[ESP_NETIF_TX]));
            printf(" - %9sps\n", format_size(netifs[i].speed[ESP_NETIF_TX]));
            printf("     recv %9s", format_size(netifs[i].total[ESP_NETIF_RX]));
            printf(" - %9sps\n", format_size(netifs[i].speed[ESP_NETIF_RX]));
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
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = pins[0];

#ifdef CONFIG_BASE_ETH_SPI
    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = pins[2],
        .miso_io_num   = pins[3],
        .sclk_io_num   = pins[4],
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
    };
    spi_device_interface_config_t dev_cfg = {
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
    if (!err) err = spi_bus_initialize(eth.bus, &bus_cfg, SPI_DMA_CH_AUTO);
#   ifdef IDF_TARGET_V4
    spi_device_handle_t spi_dev;
    if (!err) err = spi_bus_add_device(eth.bus, &dev_cfg, &spi_dev);
    eth_w5500_config_t mod_cfg = ETH_W5500_DEFAULT_CONFIG(spi_dev);
#   else
    eth_w5500_config_t mod_cfg = ETH_W5500_DEFAULT_CONFIG(eth.bus, &dev_cfg);
#   endif
    mod_cfg.int_gpio_num = pins[1];
    mac = esp_eth_mac_new_w5500(&mod_cfg, &mac_cfg);
    phy = esp_eth_phy_new_w5500(&phy_cfg);
#else // CONFIG_BASE_ETH_EMAC
    return ESP_ERR_NOT_SUPPORTED;
#endif // CONFIG_BASE_ETH_SPI

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    if (!err) err = esp_eth_driver_install(&eth_cfg, &eth.hdl);
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
#   include "esp_crc.h"
#   include "esp_now.h"
#   include "esp_wifi.h"
#   include "esp_rom_md5.h"
#   ifdef CONFIG_BASE_USE_EASYCONNECT
#       include "esp_dpp.h"
#   endif
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

static esp_err_t wifi_mode_switch(int sta, int ap) {
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
        if (EMPTY(Config.net.STA_SSID)) return err;
        ssid = Config.net.STA_SSID;
        pass = Config.net.STA_PASS;
    } else if (!pass) {
        pass = "";
    }
    if (!ip) ip = Config.net.STA_HOST;

    // WiFi mode validation
    if (( err = wifi_mode_switch(true, UNCHANGED) )) return err;
    if (getbits(STA_CONN_BIT)) {
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
    setbits(STA_STOP_BIT);
    esp_err_t err = esp_wifi_disconnect();
    if (!err && down) err = wifi_mode_switch(false, UNCHANGED);
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
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (!err) err = HAS_STA(mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (!err) err = esp_wifi_scan_stop();
    if (!err) err = esp_wifi_clear_ap_list();
    if (err) return err;
    if (!tout_ms) return esp_wifi_scan_start(&cfg, false);
    cfg.scan_time.active.min = CONS(tout_ms / 15, 10, 1400);
    cfg.scan_time.active.max = CONS(tout_ms / 10, 20, 1500);
    setbits(STA_SCAN_BIT);
    if (!( err = esp_wifi_scan_start(&cfg, true) )
     && !( err = wifi_get_ap_records() ) && verbose) {
        ESP_LOGI(TAG, "STA found %d AP", s_nap);
        wifi_print_apinfo(s_aps, s_nap);
    }
    return err;
}

esp_err_t wifi_sta_wait(uint32_t tout_ms) {
    clrbits(STA_WAIT_BIT);
    if (!waitbits(STA_WAIT_BIT, tout_ms)) return ESP_ERR_TIMEOUT;
    clrbits(STA_WAIT_BIT);
    EventBits_t bits = getbits(STA_CONN_BIT | STA_FAIL_BIT);
    if (bits & STA_FAIL_BIT) return ESP_FAIL;
    if (bits & STA_CONN_BIT) return ESP_OK;
    return ESP_ERR_INVALID_STATE; // manually disconnected
}

esp_err_t wifi_sta_list_ap() {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    printf("STA ");
    if (!HAS_STA(mode)) {
        puts("not enabled");
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen((char *)config_sta.sta.ssid))
        printf("SSID=%s ", (char *)config_sta.sta.ssid);
    if (getbits(STA_FAIL_BIT)) {
        puts("failed");
    } else if (getbits(STA_CONN_BIT)) {
        int8_t power = 0;
        uint8_t proto = 0;
        wifi_ps_type_t psave = 0;
        wifi_bandwidth_t band = WIFI_BW20;
        esp_wifi_get_ps(&psave);
        esp_wifi_get_max_tx_power(&power);
        esp_wifi_get_protocol(WIFI_IF_STA, &proto);
        esp_wifi_get_bandwidth(WIFI_IF_STA, &band);
        printf(
            "connected PSave=%s MaxTX=%ddBm\nProtocol=%c%c%c%c Bandwidth=%s\n",
            psave == WIFI_PS_MAX_MODEM ? "max" :
            psave == WIFI_PS_MIN_MODEM ? "min" : "none", power / 4,
            proto & WIFI_PROTOCOL_11B ? 'b' : ' ',
            proto & WIFI_PROTOCOL_11G ? 'g' : ' ',
            proto & WIFI_PROTOCOL_11N ? 'n' : ' ',
            proto & WIFI_PROTOCOL_LR ? 'l' : ' ',
            band == WIFI_BW20 ? "20MHz" : band == WIFI_BW40 ? "40MHz" :
            band == WIFI_BW80 ? "80MHz" : band == WIFI_BW160 ? "160MHz" :
            band == WIFI_BW_HT20 ? "HT20" : band == WIFI_BW_HT40 ? "HT40" :
            band == WIFI_BW80_BW80 ? "80+80MHZ" : ""
        );
        network_print_ipinfo(if_sta);
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
        if (EMPTY(Config.net.AP_SSID)) return err;
        ssid = Config.net.AP_SSID;
        pass = Config.net.AP_PASS;
    } else if (!pass) {
        pass = "";
    }
    if (!ip) ip = Config.net.AP_HOST;

    // WiFi mode validation
    if (( err = wifi_mode_switch(UNCHANGED, true) )) return err;

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
        // wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        // if (0 < ap->channel && ap->channel < 5) second = WIFI_SECOND_CHAN_ABOVE;
        // if (6 < ap->channel && ap->channel < 9) second = WIFI_SECOND_CHAN_BELOW;
        // if (( err = esp_wifi_set_channel(ap->channel, second) ))
        //     ESP_LOGW(TAG, "Set channel error: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_ap_stop() { return wifi_mode_switch(UNCHANGED, false); }

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
        "Protocol=%c%c%c%c Bandwidth=%s Primary=%d Second=%s\n",
        config_ap.ap.ssid_len, config_ap.ap.ssid,
        cinfo.cc[0] ?: ' ', cinfo.cc[1] ?: ' ',
        cinfo.cc[2] == ' ' ? '-' : cinfo.cc[2] ?: ' ',
        cinfo.policy == WIFI_COUNTRY_POLICY_AUTO ? 'A' : 'M',
        config_ap.ap.channel, cinfo.schan, cinfo.schan + cinfo.nchan,
        cinfo.max_tx_power / 4,
        proto & WIFI_PROTOCOL_11B ? 'b' : ' ',
        proto & WIFI_PROTOCOL_11G ? 'g' : ' ',
        proto & WIFI_PROTOCOL_11N ? 'n' : ' ',
        proto & WIFI_PROTOCOL_LR ? 'l' : ' ',
        band == WIFI_BW20 ? "20MHz" : band == WIFI_BW40 ? "40MHz" :
        band == WIFI_BW80 ? "80MHz" : band == WIFI_BW160 ? "160MHz" :
        band == WIFI_BW_HT20 ? "HT20" : band == WIFI_BW_HT40 ? "HT40" :
        band == WIFI_BW80_BW80 ? "80+80MHZ" : "", primary,
        second == WIFI_SECOND_CHAN_BELOW ? "below" :
        second == WIFI_SECOND_CHAN_ABOVE ? "above" : "HT20"
    );
    network_print_ipinfo(if_ap);

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

    char ipv4[ADDRSTRLEN];
    printf("\nAID  IP address       MAC address       RSSI Mode Mesh\n");
    LOOPN(i, sta_info_list.num) {
        uint16_t aid;
        wifi_sta_info_t *info = sta_info_list.sta + i;
        if (( err = esp_wifi_ap_get_sta_aid(info->mac, &aid) )) {
            ESP_LOGD(TAG, "Get STA AID failed: %s", esp_err_to_name(err));
            continue;
        }
        printf("%-4d %-16s " MACSTR " %4d %c%c%c%c %s\n",
            aid, esp_ip4addr_ntoa(&sta_macip_list.sta[i].ip, ipv4, sizeof(ipv4)),
            MAC2STR(info->mac), info->rssi,
            info->phy_11b ? 'b' : ' ', info->phy_11g ? 'g' : ' ',
            info->phy_11n ? 'n' : ' ', info->phy_lr ? 'l' : ' ',
            info->is_mesh_child ? "true" : "false");
    }
    return err;
}

#ifdef CONFIG_BASE_USE_EASYCONNECT
static char * dpp_qrcode = NULL;
static void cb_easyconnect(esp_supp_dpp_event_t evt, void *arg) {
    static int retry = 5;
    esp_err_t err = ESP_OK;
    if (evt == ESP_SUPP_DPP_URI_READY) {
        ESP_LOGI(TAG, "DPP raw URI: %s", (const char *)arg);
        TRYFREE(dpp_qrcode);
        size_t size = 0;
        qrenc_config_t cfg = {
            .stream = open_memstream(&dpp_qrcode, &size),
            .flags = QRENC_INV,
            .data = arg,
            .bdr = 2,
        };
        err = avi_qrenc(&cfg);
        TRYNULL(cfg.stream, fclose);
        if (err) {
            ESP_LOGW(TAG, "QRENC error: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "DPP qrcode:\n%s", dpp_qrcode);
        }
        if (( err = esp_supp_dpp_start_listen() )) {
            ESP_LOGE(TAG, "DPP start listen error: %s", esp_err_to_name(err));
            setbits(STA_WAIT_BIT);
        }
    } else if (evt == ESP_SUPP_DPP_CFG_RECVD) {
        retry = 5;
        esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t *)arg);
        esp_wifi_connect();
    } else if (evt == ESP_SUPP_DPP_FAIL) {
        ESP_LOGW(TAG, "DPP auth fail: %s", esp_err_to_name((int)arg));
        if (retry-- > 0 && !( err = esp_supp_dpp_start_listen() )) return;
        if (err) ESP_LOGE(TAG, "DPP start listen error: %s", esp_err_to_name(err));
        setbits(STA_WAIT_BIT);
    }
}
#endif

#ifdef CONFIG_BASE_USE_SMARTCONFIG
static void cb_smartconfig(void *a, esp_event_base_t b, int32_t id, void *data) {
    if (id == SC_EVENT_GOT_SSID_PSWD) {
        smartconfig_event_got_ssid_pswd_t *evt = data;
        esp_ip4_addr_t ip = { .addr = *(uint32_t *)evt->cellphone_ip };
        ESP_LOGI(TAG, "SC from " IPSTR " got SSID `%s` type %s",
                    IP2STR(&ip), (char *)evt->ssid,
                    evt->type == SC_TYPE_ESPTOUCH_AIRKISS ? "ESPTouch & AirKiss" :
                    evt->type == SC_TYPE_ESPTOUCH_V2 ? "ESPTouch V2" :
                    evt->type == SC_TYPE_AIRKISS ? "AirKiss" : "ESPTouch V1");
        wifi_sta_start((char *)evt->ssid, (char *)evt->password, NULL);
    } else if (id == SC_EVENT_SEND_ACK_DONE) {
        setbits(STA_WAIT_BIT);
    }
}
#endif

#if CONFIG_BASE_USE_EASYCONNECT || CONFIG_BASE_USE_SMARTCONFIG
static void prov_task(void *arg) {
    esp_err_t err = ESP_OK;
    uint32_t tout_ms = 600000; // 10 minutes
#   ifdef CONFIG_BASE_USE_EASYCONNECT
    if (strtob(Config.net.DPP_AUTO)) {
        err = esp_supp_dpp_init(cb_easyconnect);
        if (!err) err = esp_supp_dpp_bootstrap_gen(
            Config.net.DPP_CHAN, DPP_BOOTSTRAP_QR_CODE, NULL, NULL);
        if (!err) {
            ESP_LOGI(TAG, "EasyConnect start listening");
            err = wifi_sta_wait(tout_ms);
        }
        TRYFREE(dpp_qrcode);
        esp_supp_dpp_deinit();
        if (!err) vTaskDelete(NULL);
        ESP_LOGE(TAG, "EasyConnect error: %s", esp_err_to_name(err));
    }
#   endif
#   ifdef CONFIG_BASE_USE_SMARTCONFIG
    if (strtob(Config.net.SC_AUTO)) {
        const char *tstr = Config.net.SC_TYPE,
                   *ver2 = strcasestr(tstr, "v2"),
                   *kiss = strcasestr(tstr, "kiss"),
                  *touch = strcasestr(tstr, "touch");
        smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        smartconfig_type_t type = SC_TYPE_ESPTOUCH;
        if (kiss && touch) type = SC_TYPE_ESPTOUCH_AIRKISS;
        else if (kiss)     type = SC_TYPE_AIRKISS;
        else if (ver2)     type = SC_TYPE_ESPTOUCH_V2;
        err = esp_smartconfig_fast_mode(strcasestr(tstr, "fast"));
        if (!err) err = esp_smartconfig_set_type(type);
        if (!err) err = esp_smartconfig_start(&cfg);
        if (!err) err = REGEVT(SC, cb_smartconfig);
        if (!err) {
            ESP_LOGI(TAG, "SmartConfig %s", esp_smartconfig_get_version());
            err = wifi_sta_wait(tout_ms);
        }
        UREGEVT(SC, cb_smartconfig);
        esp_smartconfig_stop();
        if (err) ESP_LOGE(TAG, "SmartConfig error: %s", esp_err_to_name(err));
    }
#   endif
    vTaskDelete(NULL);
}
#endif

typedef struct {
    uint8_t crc, flag, payload[0];
#   define NOW_FLAG_PING BIT4
#   define NOW_FLAG_PONG BIT5
#   define NOW_FLAG_TEXT BIT6
} PACKED now_data_t;

typedef struct {
    uint16_t len;
    uint8_t flag, src[ESP_NOW_ETH_ALEN], dst[ESP_NOW_ETH_ALEN];
#   define NOW_FLAG_RECV BIT0
#   define NOW_FLAG_SEND BIT1
#   define NOW_FLAG_CAST BIT2
#   define NOW_FLAG_FAIL BIT3
    now_data_t *pkt;
} now_event_t;

static_assert(sizeof(((now_param_t *)0)->mac) == ESP_NOW_ETH_ALEN, "invalid mac size");
static_assert(sizeof(((now_param_t *)0)->lmk) == ESP_NOW_KEY_LEN, "invalid lmk size");
static_assert(ESP_ROM_MD5_DIGEST_LEN == ESP_NOW_KEY_LEN, "invalid md5 digest size");

static struct {
    QueueHandle_t queue;
    wifi_interface_t itf;
    size_t count, channel, cast_ms, ping_ms, max_plen;
    uint8_t broadcast[ESP_NOW_ETH_ALEN], buffer[ESP_NOW_MAX_DATA_LEN_V2];
    uint32_t version;
} now = {
    .itf = WIFI_IF_STA,
    .channel = 1,
    .cast_ms = 1000,
    .ping_ms = 5000,
    .broadcast = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
};

#   define MACCMP(a, b)     memcmp((a), (b), ESP_NOW_ETH_ALEN)

#   define IS_BROADCAST(m)  ( !(m) || !MACCMP(now.broadcast, m) )

#   define IS_INTERFACE(m, i) ({                                            \
        uint8_t b[ESP_NOW_ETH_ALEN];                                        \
        !esp_netif_get_mac((i), b) && !MACCMP(b, (m));                      \
    })

#   define NOW_SEND(mac, val, msg, len) ({                                  \
        now_data_t *p = (now_data_t *)now.buffer;                           \
        size_t l = MIN(now.max_plen, (len)), b = l + sizeof(now_data_t);    \
        memset(now.buffer, 0, sizeof(now.buffer));                          \
        memcpy(p->payload, (msg), l);                                       \
        p->flag = (val);                                                    \
        p->crc = esp_crc8_le(UINT8_MAX, now.buffer, b);                     \
        esp_now_send((mac), now.buffer, b);                                 \
    })

#   define NOW_RECV(msg, len) ({                                            \
        now_data_t *p = (now_data_t *)(msg);                                \
        uint8_t c = p->crc; p->crc = 0;                                     \
        c == esp_crc8_le(UINT8_MAX, (msg), (len));                          \
    })

#   define NOW_PCNT() \
    ({ esp_now_peer_num_t n; esp_now_get_peer_num(&n) ? 0 : n.total_num - 1; })

#   define NOW_ITER(peer) for (                                             \
        int n = NOW_PCNT(), e = esp_now_fetch_peer(true, &peer);            \
        n-- > 0 && !e;      e = esp_now_fetch_peer(false, &peer)            \
    )

static void cb_now_send(const uint8_t *mac, esp_now_send_status_t status) {
    now_event_t evt = {
        .flag = NOW_FLAG_SEND | (IS_BROADCAST(mac) ? NOW_FLAG_CAST : 0) |
                (status == ESP_NOW_SEND_FAIL ? NOW_FLAG_FAIL : 0)
    };
    memcpy(evt.dst, mac ?: now.broadcast, sizeof(evt.dst));
    xQueueSend(now.queue, &evt, TIMEOUT(50));
}

static void cb_now_recv(const esp_now_recv_info_t *info, const uint8_t *msg, int len) {
    if (!info || !info->src_addr || !msg || len < sizeof(now_data_t)) return;
    now_event_t evt = {
        .flag = NOW_FLAG_RECV | (IS_BROADCAST(info->des_addr) ? NOW_FLAG_CAST : 0)
    };
    if (!NOW_RECV(msg, len) || EMALLOC(evt.pkt, len)) return;
    memcpy(evt.pkt, msg, evt.len = len);
    memcpy(evt.src, info->src_addr, sizeof(evt.src));
    memcpy(evt.dst, info->des_addr ?: now.broadcast, sizeof(evt.dst));
    if (!xQueueSend(now.queue, &evt, TIMEOUT(50))) TRYFREE(evt.pkt);
}

static esp_err_t now_calc_lmk(uint8_t *lmk, const uint8_t *a, const uint8_t *b) {
    if (!lmk || !a || !b) return ESP_ERR_INVALID_ARG;
    bool first = MACCMP(a, b) < 0;
    md5_context_t ctx;
    esp_rom_md5_init(&ctx);
    esp_rom_md5_update(&ctx, Config.net.NOW_LMK, strlen(Config.net.NOW_LMK));
    esp_rom_md5_update(&ctx, first ? a : b, ESP_NOW_ETH_ALEN);
    esp_rom_md5_update(&ctx, first ? b : a, ESP_NOW_ETH_ALEN);
    esp_rom_md5_final(lmk, &ctx);
    return ESP_OK;
}

static void now_task(void *arg) {
    uint32_t last_cast = 0, last_ping = 0;
    esp_err_t err;
    now_event_t evt;
    esp_now_peer_info_t peer;
    if (( now.queue = xQueueCreate(10, sizeof(now_event_t)) ) &&
        !esp_now_register_recv_cb(cb_now_recv) &&
        !esp_now_register_send_cb(cb_now_send) &&
        !esp_wifi_set_ps(WIFI_PS_NONE)
    ) while (!getbits(NOW_STOP_BIT)) {
        uint32_t currms = MSFPO();
        if (now.cast_ms && (currms - last_cast) > now.cast_ms && now.count != 0) {
            void *msg = &now.count; size_t mlen = sizeof(now.count);
            if (( err = NOW_SEND(now.broadcast, NOW_FLAG_PING, msg, mlen) )) {
                ESP_LOGE(TAG, "NOW cast: %s", esp_err_to_name(err));
                if (err != ESP_ERR_ESPNOW_NO_MEM) now.count = 0;
            } else {
                ESP_LOGD(TAG, "NOW cast: ping-%d", now.count);
            }
            last_cast = currms;
        }
        if (now.ping_ms && (currms - last_ping) > now.ping_ms) {
            NOW_ITER(peer) { NOW_SEND(peer.peer_addr, NOW_FLAG_PING, NULL, 0); }
            last_ping = currms;
        }
        if (!xQueueReceive(now.queue, &evt, TIMEOUT(100))) continue;
        if (evt.flag & NOW_FLAG_SEND) {
            if (!(evt.flag & NOW_FLAG_FAIL)) {
                ESP_LOGD(TAG, "NOW send " MACSTR " success", MAC2STR(evt.dst));
                if (evt.flag & NOW_FLAG_CAST && now.count > 0) now.count--;
            } else if (!(evt.flag & NOW_FLAG_CAST)) {
                ESP_LOGI(TAG, "NOW del " MACSTR, MAC2STR(evt.dst));
                esp_now_del_peer(evt.dst);
            } else {
                ESP_LOGE(TAG, "NOW send " MACSTR " failed", MAC2STR(evt.dst));
                if (now.count > 0) now.count = 0;
            }
            continue;
        }
        bool broadcast = evt.flag & NOW_FLAG_CAST;
        size_t plen = evt.len - sizeof(now_data_t);
        if (!esp_now_is_peer_exist(evt.src)) {
            now_param_t param = { .apifidx = -1, .channel = -1, .encrypt = -1 };
            memcpy(param.mac, evt.src, sizeof(param.mac));
            if (!broadcast) {
                bool ap = IS_INTERFACE(evt.dst, if_ap),
                    sta = IS_INTERFACE(evt.dst, if_sta);
                param.apifidx = ap ? WIFI_IF_AP : sta ? WIFI_IF_STA : -1;
                now_calc_lmk(param.lmk, evt.src, evt.dst);
            }
            if (( err = now_command("add", &param, NULL) )) {
                ESP_LOGE(TAG, "NOW add " MACSTR ": %s",
                         MAC2STR(evt.src), esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "NOW add " MACSTR, MAC2STR(evt.src));
                if (!esp_now_get_peer(evt.src, &peer) && peer.encrypt)
                    now.count++; // latest added peer is encrypted so broadcast
            }
        }
        if (evt.pkt->flag & NOW_FLAG_PING) {
            if (plen == sizeof(now.count)) {            // broadcast
                err = NOW_SEND(evt.src, NOW_FLAG_PONG, evt.pkt->payload, plen);
            } else if (!plen) {                         // unicast
                err = NOW_SEND(evt.src, NOW_FLAG_PONG, NULL, 0);
            } else {                                    // error plen
                ESP_LOGW(TAG, "NOW recv " MACSTR ": ping plen=%d",
                         MAC2STR(evt.src), plen);
                err = ESP_OK;
            }
            if (err) ESP_LOGE(TAG, "NOW send " MACSTR ": pong err=%s",
                              MAC2STR(evt.src), esp_err_to_name(err));
        } else if (evt.pkt->flag & NOW_FLAG_PONG) {
            if (plen == sizeof(now.count)) {            // broadcast
                ESP_LOGD(TAG, "NOW recv " MACSTR ": pong-%d",
                         MAC2STR(evt.src), *(typeof(now.count)*)evt.pkt->payload);
            } else if (!plen) {                         // unicast
                ESP_LOGD(TAG, "NOW recv " MACSTR ": pong", MAC2STR(evt.src));
            } else {                                    // error plen
                ESP_LOGW(TAG, "NOW recv " MACSTR ": pong plen=%d",
                         MAC2STR(evt.src), plen);
            }
        } else if (evt.pkt->flag & NOW_FLAG_TEXT) {     // text
            ESP_LOGI(TAG, "NOW recv from " MACSTR ": %.*s",
                        MAC2STR(evt.src), plen, (char *)evt.pkt->payload);
        } else if (plen) {                              // binary
            ESP_LOGI(TAG, "NOW recv from " MACSTR ": plen %d", MAC2STR(evt.src), plen);
        }
        TRYFREE(evt.pkt);
    }
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    TRYNULL(now.queue, vQueueDelete);
    setbits(NOW_STOP_BIT);
    vTaskDelete(NULL);
}

esp_err_t now_command(const char *cmd, now_param_t *param, const char *msg) {
    esp_err_t err = ESP_OK;
    if (!now.version) {
        if (( err = esp_now_get_version(&now.version) )) return err;
        now.max_plen = (
            now.version == 2 ? ESP_NOW_MAX_DATA_LEN_V2 : ESP_NOW_MAX_DATA_LEN
        ) - sizeof(now_data_t);
    }
    TaskHandle_t task = xTaskGetHandle("espnow");
    bool hasmac = param && MACCMP(param->mac, (uint8_t[ESP_NOW_ETH_ALEN]){ 0 });
    if (cmd) {
        if (!strcasecmp(cmd, "add") || !strcasecmp(cmd, "mod")) {
            if (!param) return ESP_ERR_INVALID_ARG;
            esp_now_peer_num_t num;
            esp_now_peer_info_t peer;
            memset(&peer, 0, sizeof(peer));
            memcpy(peer.peer_addr, param->mac, sizeof(peer.peer_addr));
            bool full = esp_now_get_peer_num(&num) ||
                        num.encrypt_num >= ESP_NOW_MAX_ENCRYPT_PEER_NUM,
                exist = !esp_now_get_peer(param->mac, &peer);
            if (param->encrypt >= 0) {
                peer.encrypt = !!param->encrypt;
            } else if (!full) {
                peer.encrypt = true;
            }
            if (param->apifidx >= 0) {
                peer.ifidx = param->apifidx ? WIFI_IF_AP : WIFI_IF_STA;
            } else if (!exist) {
                peer.ifidx = now.itf;
            }
            if (param->channel >= 0) {
                peer.channel = param->channel;
            } else if (!exist && (peer.ifidx != WIFI_IF_STA || !getbits(STA_CONN_BIT))) {
                peer.channel = now.channel;
            }
            if (IS_BROADCAST(param->mac)) {
                peer.encrypt = false;
                memset(peer.lmk, 0, sizeof(peer.lmk));
            } else if (!memchr(param->lmk, '\0', sizeof(param->lmk))) {
                memcpy(peer.lmk, param->lmk, sizeof(peer.lmk));
            } else {
                uint8_t mac[6];
                esp_netif_t *ifp = peer.ifidx == WIFI_IF_AP ? if_ap : if_sta;
                if (( err = esp_netif_get_mac(ifp, mac) )) return err;
                if (( err = now_calc_lmk(peer.lmk, param->mac, mac) )) return err;
            }
            err = exist ? esp_now_mod_peer(&peer) : esp_now_add_peer(&peer);
        } else if (!strcasecmp(cmd, "del")) {
            err = param ? esp_now_del_peer(param->mac) : ESP_ERR_INVALID_ARG;
        } else if (!strcasecmp(cmd, "set")) {
            wifi_interface_t itf = param->apifidx > 0 ? WIFI_IF_AP : WIFI_IF_STA;
            uint8_t proto = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
            if (param->longrange >= 0) {
                err = esp_wifi_set_protocol(itf, proto | WIFI_PROTOCOL_LR);
            } else if (param->longrange == 0) {
                err = esp_wifi_set_protocol(itf, proto);
            }
            if (param->powersave > 0) {
                err = esp_now_set_wake_window(50) ?:
                      esp_wifi_connectionless_module_set_wake_interval(100);
            } else if (param->powersave == 0) {
                err = esp_now_set_wake_window(UINT16_MAX) ?:
                      esp_wifi_connectionless_module_set_wake_interval(
                        ESP_WIFI_CONNECTIONLESS_INTERVAL_DEFAULT_MODE);
            }
        } else if (!strcasecmp(cmd, "pmk")) {
            if (param && !memchr(param->lmk, '\0', sizeof(param->lmk))) {
                if (!( err = esp_now_set_pmk(param->lmk) ))
                    ESP_LOGI(TAG, "NOW set pmk: %s",
                             format_sha256(param->lmk, sizeof(param->lmk)));
            } else if (strlen(msg ?: "") == ESP_NOW_KEY_LEN) {
                if (!( err = esp_now_set_pmk((const uint8_t *)msg) ))
                    ESP_LOGI(TAG, "NOW set pmk: %s", msg);
            } else {
                err = msg ? ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_ARG;
            }
        } else if (!strcasecmp(cmd, "send")) {
            size_t mlen = strlen(msg ?: "");
            if (!mlen) return ESP_ERR_INVALID_SIZE;
            if (hasmac && !esp_now_is_peer_exist(param->mac) &&
                ( err = now_command("add", param, NULL) )) return err;
            err = NOW_SEND(hasmac ? param->mac : NULL, NOW_FLAG_TEXT, msg, mlen);
        } else if (!strcasecmp(cmd, "cast")) {
            size_t mlen = strlen(msg ?: "");
            if (param && param->broadcast >= 0) {
                now.count = param->broadcast;
            } else if (mlen) {
                err = NOW_SEND(now.broadcast, NOW_FLAG_TEXT, msg, mlen);
            }
        } else if (!strtob(cmd)) {
            setbits(NOW_STOP_BIT);
        } else if (!task) {
            int ap = param && param->apifidx > 0 ? true : UNCHANGED,
                sta = ap == UNCHANGED ? true : UNCHANGED;
            now_param_t tmp = { .apifidx = -1, .channel = -1, .encrypt = -1 };
            if (param) memcpy(&tmp, param, sizeof(tmp));
            memcpy(tmp.mac, now.broadcast, sizeof(tmp.mac));
            if (( err = wifi_mode_switch(sta, ap) ) || ( err = esp_now_init()) ||
                ( err = now_command("add", &tmp, NULL) )) return err;
            clrbits(NOW_STOP_BIT);
            xTaskCreate(now_task, "espnow", 8192, NULL, 4, &task);
            if (!task) {
                err = ESP_ERR_NO_MEM;
            } else if (waitbits(NOW_STOP_BIT, 100)) {
                err = ESP_FAIL;
            } else {
                now.count = 3;
            }
        }
    } else if (param && (param->longrange >= 0 || param->powersave >= 0)) {
        err = now_command("set", param, msg);
    } else if (msg) {
        err = now_command("send", param, msg);
    } else {
        esp_now_peer_info_t peer;
        int num = task ? NOW_PCNT() : 0, idx = 0;
        if (!hasmac || !task)
            printf("ESPNow V%" PRIu32 " %sabled peers %d count %u\n",
                   now.version, task ? "en" : "dis", num, now.count);
        if (!num) return err;
        printf("ID %-17s CH ITF ENC LMK\n", "MAC");
        NOW_ITER(peer) {
            if (hasmac && MACCMP(param->mac, peer.peer_addr)) continue;
            printf("%2d " MACSTR " %2d %3s %s %s\n",
                idx++, MAC2STR(peer.peer_addr), peer.channel,
                peer.ifidx == WIFI_IF_AP ? "AP" : "STA",
                peer.encrypt ? "yes" : "no",
                format_sha256(peer.lmk, sizeof(peer.lmk)));
        }
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
    } else if (getbits(STA_CONN_BIT)) {
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
    EventBits_t bits = waitbits(FTM_DONE_BIT | FTM_FAIL_BIT, 3000);
    clrbits(bits);
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
            ESP_LOGI(TAG, "AP client " IPSTR " assigned", IP2STR(&evt->ip));
        } else if (id == IP_EVENT_STA_LOST_IP) {
            if (esp_netif_is_netif_up(if_eth)) esp_netif_set_default_netif(if_eth);
        } else if (id == IP_EVENT_ETH_LOST_IP) {
            if (esp_netif_is_netif_up(if_sta)) esp_netif_set_default_netif(if_sta);
        } else if (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP) {
            ip_event_got_ip_t *evt = data;
            esp_netif_set_default_netif(evt->esp_netif);
            esp_netif_create_ip6_linklocal(evt->esp_netif); // hotfix
            if (evt->ip_changed) {
                ESP_LOGI(TAG, "IF %s IP " IPSTR ", GW " IPSTR ", Mask " IPSTR,
                    esp_netif_get_desc(evt->esp_netif), IP2STR(&evt->ip_info.ip),
                    IP2STR(&evt->ip_info.gw), IP2STR(&evt->ip_info.netmask));
            }
            if (strtob(Config.app.OTA_AUTO)) update_control(NULL);
            if (strtob(Config.app.MQTT_RUN)) mqtt_control("on");
            if (strtob(Config.app.SNTP_RUN)) sntp_control("sync");
            if (strtob(Config.app.HBT_RUN)) hbeat_control("on");
#ifdef CONFIG_BASE_USE_WIFI
            if (strtob(Config.net.AP_AUTO)) wifi_ap_stop();
            if (strtob(Config.net.AP_NAPT)) wifi_update_napt(evt->esp_netif);
            if (id == IP_EVENT_STA_GOT_IP) {
                setbits(STA_CONN_BIT | STA_WAIT_BIT);
                clrbits(STA_FAIL_BIT | STA_STOP_BIT);
            }
#endif
        } else if (id == IP_EVENT_GOT_IP6) {
            ip_event_got_ip6_t *evt = data;
            ESP_LOGI(TAG, "IF %s IP[%d] " IPV6STR " (%s)",
                esp_netif_get_desc(evt->esp_netif), evt->ip_index,
                IPV62STR(evt->ip6_info.ip), network_ipv6_tstr(&evt->ip6_info.ip));
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
            clrbits(STA_CONN_BIT);
            wifi_event_sta_disconnected_t *evt = data;
            if (getbits(STA_STOP_BIT)) {
                ESP_LOGI(TAG, "STA disconnect from `%s`", evt->ssid);
                clrbits(STA_STOP_BIT);
                setbits(STA_WAIT_BIT);
            } else if (evt->reason != WIFI_REASON_NO_AP_FOUND && retry < 3) {
                ESP_LOGD(TAG, "STA connect `%s` retry %d", evt->ssid, retry);
                retry++;
                esp_wifi_connect();
            } else {
                ESP_LOGW(TAG, "STA connect `%s` failed %d", evt->ssid, evt->reason);
                retry = 0;
                setbits(STA_WAIT_BIT | STA_FAIL_BIT);
                if (strtob(Config.net.AP_AUTO)) wifi_ap_start(NULL, NULL, NULL);
                if (strtob(Config.net.DPP_AUTO) || strtob(Config.net.SC_AUTO))
                    network_command("sta", "prov", "on", NULL, NULL, 0);
            }
        } else if (id == WIFI_EVENT_STA_NEIGHBOR_REP) {
            wifi_event_neighbor_report_t *evt = data;
            LOOPN(i, evt->report_len) {
                ESP_LOGI(TAG, "STA neighbor[%d] report %d", i, evt->n_report[i]);
            }
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            if (getbits(STA_SCAN_BIT)) {
                clrbits(STA_SCAN_BIT);
            } else if (!wifi_get_ap_records()) {
                ESP_LOGI(TAG, "STA found %d AP", s_nap);
                wifi_print_apinfo(s_aps, s_nap);
            }
        } else if (id == WIFI_EVENT_FTM_REPORT) {
            wifi_event_ftm_report_t *evt = data;
            if (evt->status != FTM_STATUS_SUCCESS) {
                ESP_LOGW(TAG, "FTM " MACSTR " failed: %s",
                         MAC2STR(evt->peer_mac), wifi_ftm_str(evt->status));
                setbits(FTM_FAIL_BIT);
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
                setbits(FTM_DONE_BIT);
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
#ifdef CONFIG_BASE_USE_SMARTCONFIG
        "smartconfig",
#endif
    };
    ITERV(tag, tags) { esp_log_level_set(tag, ESP_LOG_WARN); }

    ESP_ERROR_CHECK( (evtgrp = xEventGroupCreate()) ? ESP_OK : ESP_ERR_NO_MEM );
#ifdef CONFIG_BASE_USE_ETH
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    if_eth = esp_netif_new(&netif_config);
    ESP_ERROR_CHECK( network_eth_init() );
    ESP_ERROR_CHECK( esp_netif_attach(if_eth, eth.glue) );
    ESP_ERROR_CHECK( esp_netif_tx_rx_event_enable(if_eth) );
    ESP_ERROR_CHECK( esp_eth_start(eth.hdl) );
    ESP_ERROR_CHECK( REGEVT(ETH, cb_network) );
#endif
#ifdef CONFIG_BASE_USE_WIFI
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    if_ap = esp_netif_create_default_wifi_ap();
    if_sta = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK( esp_netif_tx_rx_event_enable(if_ap) );
    ESP_ERROR_CHECK( esp_netif_tx_rx_event_enable(if_sta) );
    ESP_ERROR_CHECK( esp_wifi_init(&init_config) );
    ESP_ERROR_CHECK( esp_wifi_get_config(WIFI_IF_AP, &config_ap) );
    ESP_ERROR_CHECK( esp_wifi_get_config(WIFI_IF_STA, &config_sta) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( REGEVT(WIFI, cb_network) );
#endif
    ESP_ERROR_CHECK( REGEVT(IP, cb_network) );

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
    if (EMPTY(iface)) return dft;
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
    esp_err_t err = ifp ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (err || ( err = esp_netif_get_ip_info(ifp, &ip) )) return err;
    err = ESP_ERR_NOT_SUPPORTED;
    if (!strcasecmp(cmd, "dft")) {
        err = esp_netif_set_default_netif(ifp);
    } else if (!strcasecmp(cmd, "scan")) {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_sta) return wifi_sta_scan(ssid, 0, tout_ms, true);
#endif
    } else if (!strcasecmp(cmd, "prov")) {
#if CONFIG_BASE_USE_EASYCONNECT || CONFIG_BASE_USE_SMARTCONFIG
        if (ifp != if_sta) return err;
        if (!strcasecmp(ssid ?: "", "qrcode")) {
#   ifdef CONFIG_BASE_USE_EASYCONNECT
            if (!dpp_qrcode) return ESP_ERR_INVALID_STATE;
            puts(dpp_qrcode); return ESP_OK;
#   else
            return ESP_ERR_NOT_SUPPORTED;
#   endif
        }
        if (getbits(STA_CONN_BIT)) {
            if (( err = wifi_sta_stop(false) )) return err;
            err = wifi_sta_wait(tout_ms ?: 100);
            if (err == ESP_ERR_TIMEOUT || !err) return err ?: ESP_FAIL;
        }
        TaskHandle_t task = xTaskGetHandle("prov");
        if (!strtob(ssid)) {
            setbits(STA_WAIT_BIT);
        } else if (!task) {
            xTaskCreate(prov_task, "prov", 4096, NULL, 5, &task);
            if (!task) return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
#endif
    } else if (!strcasecmp(cmd, "txrx")) {
#ifdef CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
        if (strtob(ssid)) return esp_netif_tx_rx_event_enable(ifp);
        return esp_netif_tx_rx_event_disable(ifp);
#endif
    } else if (!strcasecmp(cmd, "info") || !strcasecmp(cmd, "list")) {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_ap)  return wifi_ap_list_sta();
        if (ifp == if_sta) return wifi_sta_list_ap();
#endif
    } else if (!strcasecmp(cmd, "ip") || !strcasecmp(cmd, "static")) {
        if (!ipv4) {
            printf(IPSTR "\n", IP2STR(&ip.ip));
            return ESP_OK;
        }
        err = network_ctrl_static(ifp, ipv4, NULL, NULL);
    } else if (!strcasecmp(cmd, "gw") || !strcasecmp(cmd, "gateway")) {
        if (!ipv4) {
            printf(IPSTR "\n", IP2STR(&ip.gw));
            return ESP_OK;
        }
        err = network_ctrl_static(ifp, NULL, ipv4, NULL);
    } else if (!strcasecmp(cmd, "nm") || !strcasecmp(cmd, "netmask")) {
        if (!ipv4) {
            printf(IPSTR "\n", IP2STR(&ip.netmask));
            return ESP_OK;
        }
        err = network_ctrl_static(ifp, NULL, NULL, ipv4);
    } else if (!strcasecmp(cmd, "up") || (strcasecmp(cmd, "down") && strtob(cmd))) {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_ap) {
            err = wifi_ap_start(ssid, pass, ipv4);
        } else if (ifp == if_sta) {
            err = wifi_sta_start(ssid, pass, ipv4);
            if (!err && tout_ms) err = wifi_sta_wait(tout_ms);
        }
#endif
    } else {
#ifdef CONFIG_BASE_USE_WIFI
        if (ifp == if_ap) {
            err = wifi_ap_stop();
        } else if (ifp == if_sta) {
            err = wifi_sta_stop(!strcasecmp(cmd, "down"));
            if (!err && tout_ms) {
                err = wifi_sta_wait(tout_ms);
                err = err == ESP_ERR_TIMEOUT ? err : !err;
            }
        }
#endif
    }
    if (err) return err;
exit:
    return network_print_detail(itf);
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
            uint32_t saddr = network_local_ipv4(NULL, NULL);
            if (!saddr) return ESP_ERR_INVALID_STATE;
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
            esp_ip4_addr_t daddr = { .addr = ent->daddr };
            printf("%4s %5u %5u " IPSTR "\n",
                   ent->proto == IPPROTO_TCP ? "TCP" : "UDP",
                   ent->sport, ent->dport, IP2STR(&daddr));
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
} pcap = { .tout = TIMEOUT(50) };

#   ifdef CONFIG_BASE_USE_WIFI
static void cb_pcap_wifi(void *buf, wifi_promiscuous_pkt_type_t type) {
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
static esp_err_t cb_pcap_eth(
    esp_eth_handle_t hdl, uint8_t *buf, uint32_t len, void *arg
) {
    const struct timeval *tv = get_systime_us();
    pcap_pkt_t record = { buf, len, tv->tv_sec, tv->tv_usec };
    if (!xQueueSend(pcap.queue, &record, pcap.tout)) free(record.buffer);
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
        if (!err) err = esp_wifi_set_promiscuous_rx_cb(cb_pcap_wifi);
        if (!err) err = esp_wifi_set_promiscuous(true);
#   endif
#   ifdef CONFIG_BASE_USE_ETH
    } else if (pcap.type == PCAP_LINK_TYPE_ETHERNET) {
        bool enable = true;
        if (!err) err = esp_eth_update_input_path(eth.hdl, cb_pcap_eth, NULL);
        if (!err) err = esp_eth_ioctl(eth.hdl, ETH_CMD_S_PROMISCUOUS, &enable);
#   endif
    } else {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err) goto clean;
    pcap_pkt_t pkt;
    uint32_t cnt = 0;
    clrbits(PCAP_STOP_BIT);
    while (!getbits(PCAP_STOP_BIT)) {
        if (!xQueueReceive(pcap.queue, &pkt, pcap.tout)) continue;
        pcap_capture_packet(
            pcap.hdl, pkt.buffer, pkt.length,
            pkt.seconds, pkt.microseconds
        );
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
    setbits(PCAP_STOP_BIT);
    vTaskDelete(NULL);
}

esp_err_t pcap_command(const char *ctrl, const char *itf, uint32_t npkt) {
    TaskHandle_t task = xTaskGetHandle("pcap");
    if (ctrl) {
        if (!strtob(ctrl)) {
            setbits(PCAP_STOP_BIT);
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
            if (waitbits(PCAP_STOP_BIT, 100)) return ESP_FAIL;
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
        setbits(MQTT_CONN_BIT);
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
    case MQTT_EVENT_DISCONNECTED: clrbits(MQTT_CONN_BIT); break;
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
            bool connected = getbits(MQTT_CONN_BIT);
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
            if (!pub || !pub->msg || !pub->len) return ESP_ERR_INVALID_ARG;
            if (!topic) {
                if (cJSON_GetArraySize(mqtt.tpc) != 1) return ESP_ERR_INVALID_ARG;
                topic = mqtt.tpc->child->string; // use first topic
            }
            if (( msgid = esp_mqtt_client_publish(
                mqtt.hdl, topic, pub->msg, pub->len, pub->qos, pub->retain
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
                clrbits(MQTT_CONN_BIT);
                for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
                    cJSON_SetNumberValue(item, 0);
                }
            }
        } else if (!getbits(MQTT_CONN_BIT)) {
            err = esp_mqtt_client_start(mqtt.hdl);
            waitbits(MQTT_CONN_BIT, 3000);
        }
    } else if (pub) {
        err = mqtt_command("pub", NULL, NULL, pub);
    } else if (!mqtt.hdl || !mqtt.ctx) {
        printf("MQTT not %s\n", mqtt.hdl ? "loaded" : "inited");
    } else {
        printf("MQTT %sconnected to %s\n",
               getbits(MQTT_CONN_BIT) ? "" : "not ", mqtt.url->valuestring);
        for (cJSON *item = mqtt.tpc->child; item; item = item->next) {
            printf(" - %5d: %s\n", item->valueint, item->string);
        }
    }
    return err;
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
    esp_ip4_addr_t ip4;
    esp_ip6_addr_t ip6;
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

static void cb_sntp_notification(struct timeval *tv) {
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
    if (idx > SNTP_MAX_SERVERS || EMPTY(host)) return err;
    bool updated = false;
#if SNTP_SERVER_DNS
    if (!address_like_numeric(host)) {
        esp_sntp_setservername(idx, host);
        updated = true;
    } else
#endif
    if (!err) {
        ip_addr_t addr = { 0 };
        if (( err = network_parse_ipaddr(host, &addr) )) return err;
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
            esp_sntp_set_time_sync_notification_cb(cb_sntp_notification);
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
    if (network_parse_ipaddr(host, &cfg.target_addr)) {
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
    esp_err_t err = ESP_OK;
    TaskHandle_t server = xTaskGetHandle(IPERF_TRAFFIC_TASK_NAME);
    TaskHandle_t client = xTaskGetHandle(IPERF_REPORT_TASK_NAME);
    if (abort) {
        if (!( err = iperf_stop_instance(IPERF_ALL_INSTANCES_ID) )) {
            if (server) puts("IPerf server stopped");
            if (client) puts("IPerf client stopped");
        }
    } else if (server || client) {
        printf("IPerf %s running\n", server ? "server" : "client");
    } else if (host && !*host) {
        puts("IPerf not running");
    } else {
        ip_addr_t dst_addr;
        iperf_cfg_t cfg = {
            .format = KBYTES_PER_SEC,
            .flag = (host ? IPERF_FLAG_CLIENT : IPERF_FLAG_SERVER) |
                    (udp ? IPERF_FLAG_UDP : IPERF_FLAG_TCP),
            .interval = intv_sec ?: IPERF_DEFAULT_INTERVAL,
            .time = (tout_sec ?: IPERF_DEFAULT_TIME) + (host ? 0 : 1),
            .bw_lim = IPERF_DEFAULT_NO_BW_LIMIT,
            .dport = (port && host) ? port : IPERF_DEFAULT_PORT,
            .sport = (port && !host) ? port : IPERF_DEFAULT_PORT,
            .len_send_buf = length,
        };
        if (cfg.time < cfg.interval) cfg.time = cfg.interval;
        if (!host) {                            // server mode
            if (network_local_ipaddr(NULL, ESP_IPADDR_TYPE_ANY, &cfg.source) < 0)
                return ESP_ERR_INVALID_STATE;   // not connected
        } else if (( err = network_parse_ipaddr(host, &dst_addr) )) {
            return err;                         // invalid host
        } else if (dst_addr.type == IPADDR_TYPE_V4) {
            cfg.destination.type = cfg.source.type = ESP_IPADDR_TYPE_V4;
            cfg.destination.u_addr.ip4.addr = dst_addr.u_addr.ip4.addr;
            if (!network_local_ipv4(NULL, &cfg.source.u_addr.ip4))
                return ESP_ERR_INVALID_STATE;   // no ipv4
        } else if (dst_addr.type == IPADDR_TYPE_V6) {
            cfg.destination.type = cfg.source.type = ESP_IPADDR_TYPE_V6;
            memcpy(cfg.destination.u_addr.ip6.addr, dst_addr.u_addr.ip6.addr, 16);
            if (!network_local_ipv6(NULL, &cfg.source.u_addr.ip6))
                return ESP_ERR_INVALID_STATE;   // no ipv6
        }
        if (iperf_start_instance(&cfg) < 0) return ESP_FAIL;
        if (cfg.source.type == ESP_IPADDR_TYPE_V4) {
            ESP_LOGI(TAG,
                "IPerf %s-%s src=" IPSTR ":%u, dst=" IPSTR ":%u,"
                " intv=%" PRIu32 ", tout=%" PRIu32,
                udp ? "udp" : "tcp", host ? "client" : "server",
                IP2STR(&cfg.source.u_addr.ip4), cfg.sport,
                IP2STR(&cfg.destination.u_addr.ip4), cfg.dport,
                cfg.interval, cfg.time);
        } else {
            ESP_LOGI(TAG,
                "IPerf %s-%s src=" IPV6STR ":%u, dst=" IPV6STR ":%u,"
                " intv=%" PRIu32 ", tout=%" PRIu32,
                udp ? "udp" : "tcp", host ? "client" : "server",
                IPV62STR(cfg.source.u_addr.ip6), cfg.sport,
                IPV62STR(cfg.destination.u_addr.ip6), cfg.dport,
                cfg.interval, cfg.time);
        }
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
    char host[ADDRSTRLEN], port[6];
    uint32_t tout;
} timesync_param_t;

static void timesync_server_task(void *arg) {
    timesync_param_t *param = arg;
    if (timesync_server_init(param->port)) {
        ESP_LOGE(TAG, "Could not init TimeSync Server");
    } else {
        clrbits(TSS_STOP_BIT);
        while (!getbits(TSS_STOP_BIT)) {
            timesync_server_loop(param->tout);
        }
    }
    free(param);
    timesync_server_exit();
    setbits(TS_STOPPED_BIT | TSS_STOP_BIT);
    vTaskDelete(NULL);
}

static void timesync_client_task(void *arg) {
    timesync_param_t *param = arg;
    if (timesync_client_init(param->host, param->port)) {
        ESP_LOGE(TAG, "Could not init TimeSync Client");
    } else {
        double offset = 0;
        clrbits(TSC_STOP_BIT);
        while (!getbits(TSC_STOP_BIT)) {
            if (timesync_client_xsync(&offset, 5) < 0) break;
            ESP_LOGI(TAG, "TimeSync offset with %s: %.9f", param->host, offset);
            waitbits(TSC_STOP_BIT, param->tout);
        }
    }
    free(param);
    timesync_client_exit();
    setbits(TS_STOPPED_BIT | TSC_STOP_BIT);
    vTaskDelete(NULL);
}

esp_err_t tsync_command(
    const char *host, const char *port, uint32_t tout_ms, bool abort
) {
    TaskHandle_t server = xTaskGetHandle("tss");
    TaskHandle_t client = xTaskGetHandle("tsc");
    if (abort) {
        // do NOT use vTaskDelete because we need clean on exit
        setbits(TSS_STOP_BIT | TSC_STOP_BIT);
        if (waitbits(TS_STOPPED_BIT, tout_ms ?: 1000)) {
            if (server) puts("TimeSync server stopped");
            if (client) puts("TimeSync client stopped");
        }
    } else if (server || client) {
        printf("TimeSync %s running\n", server ? "server" : "client");
    } else if (host && !*host) {
        puts("TimeSync not running");
    } else {
        timesync_param_t *param = calloc(1, sizeof(timesync_param_t));
        if (!param) return ESP_ERR_NO_MEM;
        if (host) strlcpy(param->host, host, sizeof(param->host));
        if (port) strlcpy(param->port, port, sizeof(param->port));
        else snprintf(param->port, sizeof(param->port), "%d", TIMESYNC_PORT);
        param->tout = tout_ms ?: (host ? 30000 : 500); // client 30s, server 0.5s
        TaskHandle_t task = NULL;
        clrbits(TS_STOPPED_BIT);
        if (host) {
            xTaskCreate(timesync_client_task, "tsc", 4096, param, 5, &task);
        } else {
            xTaskCreate(timesync_server_task, "tss", 4096, param, 5, &task);
        }
        if (!task) { free(param); return ESP_ERR_NO_MEM; }
        if (waitbits(TS_STOPPED_BIT, 100)) return ESP_FAIL;
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
    int blen = 512, rlen = 2048;
    char mac[9], macstr[32], addr[ADDRSTRLEN], *body = NULL, *resp = NULL;
    cJSON *info = cJSON_CreateObject(), *rst = NULL, *ts = NULL, *ip = NULL;
    if (( err = EMALLOC(body, blen) )
     || ( err = EMALLOC(resp, rlen) )
     || ( err = esp_base_mac_addr_get((uint8_t *)mac) )
    ) goto error;
    memset(addr, ' ', sizeof(addr) - 1); addr[sizeof(addr) - 1] = '\0';
    snprintf(macstr, sizeof(macstr), MACSTR, MAC2STR(mac));
    cJSON_AddStringToObject(info, "sn", Config.info.UID);
    cJSON_AddStringToObject(info, "mac_address", macstr);
    ts = cJSON_AddStringToObject(info, "beat_time", "XXXX-XX-XX XX:XX:XX");
    ip = cJSON_AddStringToObject(info, "ip_address", addr);
    if (!info || !ts || !ip) {
        err = ESP_ERR_NO_MEM;
        goto error;
    }
    if (EMPTY(hbeat.hbturl)) {
        if (EMPTY(Config.app.HBT_URL)) {
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
    clrbits(HBT_STOP_BIT);
    TickType_t last[2] = { 0, 0 };
    while (hbeat.hbtime > 0 && !getbits(HBT_STOP_BIT)) {
        waitbits(HBT_STOP_BIT, 500);
        TickType_t curr = xTaskGetTickCount(),
                   dhbt = TIMEOUT((int)(hbeat.hbtime * 1000)),
                   ditv = TIMEOUT((int)(hbeat.intval * 1000));
        if (!last[0] || (curr - last[0]) >= dhbt) {
            last[0] = curr;
            time_t sec = get_timestamp_us(NULL);
            if (!network_local_ipstr(NULL, ip->valuestring, sizeof(addr)))
                continue;
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
        if (EMPTY(hbeat.imgurl) || hbeat.intval <= 0) continue;
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
                network_local_ipstr(NULL, NULL, 0), macstr);
            size_t wlen = strlen(body) + frame.len + 13;
            esp_http_client_set_url(client, hbeat.imgurl);
            esp_http_client_set_header(client, "Content-Type", CTYPE_BDRY(FRAME));
            esp_http_client_set_timeout_ms(client, 10000);
            if (( err = esp_http_client_open(client, wlen) )) {
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
    setbits(HBT_STOP_BIT);
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
            setbits(HBT_STOP_BIT);
        } else if (!task) {
            xTaskCreate(hbeat_task, "hbeat", 4096, NULL, 10, &task);
            err = !task ? ESP_ERR_NO_MEM : waitbits(HBT_STOP_BIT, 100) ? ESP_FAIL : ESP_OK;
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
