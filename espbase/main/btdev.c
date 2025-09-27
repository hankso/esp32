/*
 * File: btdev.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/14 7:41:42
 */

#include "btmode.h"
#include "config.h"

#ifdef CONFIG_BASE_USE_BT

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_mac.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_hidd.h"
#include "esp_hidd_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"

/*
 * Utilities
 */

#define BDASTR              ESP_BD_ADDR_STR
#define BDA2STR             ESP_BD_ADDR_HEX
#define HAS_BT(m)           ( (m) & ESP_BT_MODE_CLASSIC_BT )
#define HAS_BLE(m)          ( (m) & ESP_BT_MODE_BLE )

#define BT_SCAN_DONE_BIT    BIT0
#define BT_SCAN_BLOCK_BIT   BIT1
#define BLE_SCAN_DONE_BIT   BIT2
#define BLE_SCAN_BLOCK_BIT  BIT3

#define BT_IDLE() \
    ( esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE )
#define BT_INITED() \
    ( esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED )
#define BT_ENABLED() \
    ( esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED )

// defined in bthost.c
esp_err_t bthost_connect(esp_bd_addr_t, esp_bt_dev_type_t, uint8_t);

static struct {
    bool enabled;
    bool connected;
    char name[64];
    esp_bt_mode_t mode;             // esp_bt
    esp_bd_addr_t addr;
    esp_hidd_qos_param_t qos;       // bluedroid
    esp_hidd_app_param_t app;
    esp_bt_connection_mode_t cmode;
    esp_bt_discovery_mode_t dmode;
    EventGroupHandle_t evtgrp;
    esp_hidd_dev_t *hiddev;         // esp_hid
} ctx;

static EventBits_t waitBits(EventBits_t bits, uint32_t ms) {
    // ClearOnExit = true, WaitForAllBits = false
    return ctx.evtgrp ? xEventGroupWaitBits(
        ctx.evtgrp, bits, pdTRUE, pdFALSE, TIMEOUT(ms)) & bits : 0;
}

static EventBits_t getBits(EventBits_t bits) {
    return ctx.evtgrp ? xEventGroupGetBits(ctx.evtgrp) & bits : 0;
}

static bool setBits(EventBits_t bits) {
    return ctx.evtgrp ? xEventGroupSetBits(ctx.evtgrp, bits) : false;
}

static EventBits_t clearBits(EventBits_t bits) {
    return ctx.evtgrp ? xEventGroupClearBits(ctx.evtgrp, bits) : 0;
}

void btdev_status(btmode_t mode) {
    printf("Connectable: %s, discoverable: %s, evtgrp: 0b%s\n",
        ctx.cmode ? "true" : "false",
        ctx.dmode == ESP_BT_GENERAL_DISCOVERABLE
            ? "general"
            : (ctx.dmode ? "limited " : "false"),
        format_binary(getBits(0xFF), 8));
#ifdef CONFIG_BASE_BT_HID_DEVICE
    if (mode == BT_HIDD) {
        if (!ctx.enabled) {
            puts("Application not registered");
        } else {
            printf("Application: %s\n", ctx.app.description);
        }
        int num = esp_bt_gap_get_bond_device_num();
        esp_bd_addr_t *addrs = calloc(num, sizeof(esp_bd_addr_t));
        if (num && addrs && !esp_bt_gap_get_bond_device_list(&num, addrs)) {
            printf("Bonded list: %d\n", num);
            LOOPN(i, num) {
                int j = !memcmp(addrs[i], ctx.addr, sizeof(ctx.addr));
                printf("  %c " BDASTR, "-*"[j], BDA2STR(addrs[i]));
                if (j && strlen(ctx.name)) printf(" (%s)", ctx.name);
                putchar('\n');
            }
        }
        TRYFREE(addrs);
        if (!ctx.connected) puts("Not connected");
    }
#endif
#ifdef CONFIG_BASE_BLE_HID_DEVICE
    if (mode == BLE_HIDD) {
        if (!ctx.enabled) {
            puts("GATTS service not started");
        } else {
            printf("Application: %s\n", HIDTool.dstr);
        }
        int num = esp_ble_get_bond_device_num();
        esp_ble_bond_dev_t *addrs = calloc(num, sizeof(esp_ble_bond_dev_t));
        if (num && addrs && !esp_ble_get_bond_device_list(&num, addrs)) {
            printf("Bonded list: %d\n", num);
            LOOPN(i, num) {
                int j = !memcmp(addrs[i].bd_addr, ctx.addr, sizeof(ctx.addr));
                printf("  %c " BDASTR "\n", "-*"[j], BDA2STR(addrs[i].bd_addr));
            }
        }
        TRYFREE(addrs);
        if (!ctx.connected) puts("Not connected");
    }
#endif
}

static const char * uuid_str(esp_bt_uuid_t *uuid) {
    static char buf[37];
    if (uuid->len == 2) {
        snprintf(buf, sizeof(buf), "%04X", uuid->uuid.uuid16);
    } else if (uuid->len == 4) {
        snprintf(buf, sizeof(buf), "%08" PRIX32, uuid->uuid.uuid32);
    } else if (uuid->len == 16) {
        size_t size = 0;
        LOOPND(i, uuid->len) {
            size += sprintf(buf + size, "%02x", uuid->uuid.uuid128[i]);
            if (i == 12 || i == 10 || i == 8 || i == 6) buf[size++] = '-';
        }
    } else {
        buf[0] = '\0';
    }
    return buf;
}

/*
 * BT & BLE scan
 */

typedef struct scan_rst {
    struct scan_rst *next;
    char name[64];
    int8_t rssi;
    esp_bd_addr_t addr;
    esp_bt_dev_type_t dev_type;
    union {
        struct {
            uint32_t cod;
            esp_bt_uuid_t uuid;
        } bt;
        struct {
            uint16_t gatts_uuid;
            uint16_t appearance;
            esp_ble_addr_type_t addr_type;
        } ble;
    };
} scan_rst_t;

static scan_rst_t *s_devs;

scan_rst_t *btmode_find_device(const char *name, uint8_t *bda) {
    scan_rst_t *ptr = s_devs;
    if (!bda) {
        if (!name) return ptr;
        int num = 0; LOOPN(i, strlen(name)) { if (name[i] == ':') num++; }
        if (strlen(name) == 17 && num == 5) {
            esp_bd_addr_t addr;
            LOOPN(i, LEN(addr)) {
                char buf[5] = { '0', 'x', name[3 * i], name[3 * i + 1], 0 };
                addr[i] = strtol(buf, NULL, 16);
            }
            bda = addr;
            name = NULL;
        }
    }
    while (ptr) {
        if (name && !strcmp(ptr->name, name)) break;
        if (bda && !memcmp(ptr->addr, bda, sizeof(ptr->addr))) break;
        ptr = ptr->next;
    }
    return ptr;
}

static UNUSED size_t scan_rst_num(scan_rst_t *ptr) {
    size_t num = 0;
    for (ptr = ptr ?: s_devs; ptr; ptr = ptr->next) { num++; }
    return num;
}

static UNUSED void scan_print_devinfo(scan_rst_t *devs, int count) {
    size_t maxlen = 16, num = 0, uuidlen = 2;
    scan_rst_t *ptr = devs;
    while (ptr && (count < 0 || num < count)) {
#ifdef CONFIG_BASE_AUTO_ALIGN
        maxlen = MAX(maxlen, strlen(ptr->name));
        if (ptr->dev_type == ESP_BT_DEVICE_TYPE_BREDR)
            uuidlen = MAX(uuidlen, ptr->bt.uuid.len);
#endif
        ptr = ptr->next;
        num++;
    }
    if (count < 0 || count > num) count = num;
    if (!count) return;
    printf("Type Name%*s %-17s RSSI %*s CoD/Usage\n",
           maxlen - 4, "", "MAC address", 2 * uuidlen, "UUID");
    for (ptr = devs; count; count--, ptr = ptr->next) {
        const char *uuidstr = uuid_str(&ptr->bt.uuid);
        if (ptr->dev_type & ESP_BT_DEVICE_TYPE_BLE && ptr->ble.gatts_uuid) {
            esp_bt_uuid_t uuid = {
                .len = 2, .uuid.uuid16 = ptr->ble.gatts_uuid
            };
            uuidstr = uuid_str(&uuid);
        }
        printf("%-4s %-*s " BDASTR " %4d %*s ",
               ptr->dev_type == ESP_BT_DEVICE_TYPE_BREDR
                    ? "BT" : ptr->dev_type == ESP_BT_DEVICE_TYPE_BLE
                    ? "BLE" : "DUMO",
               maxlen, ptr->name, BDA2STR(ptr->addr),
               ptr->rssi, 2 * uuidlen, uuidstr);
        if (ptr->dev_type & ESP_BT_DEVICE_TYPE_BLE) {
            switch (ptr->ble.appearance) {
            case ESP_HID_APPEARANCE_GENERIC:    puts("Generic"); break;
            case ESP_HID_APPEARANCE_KEYBOARD:   puts("Keyboard"); break;
            case ESP_HID_APPEARANCE_MOUSE:      puts("Mouse"); break;
            case ESP_HID_APPEARANCE_JOYSTICK:   puts("Joystick"); break;
            case ESP_HID_APPEARANCE_GAMEPAD:    puts("Gamepad"); break;
            default:                            putchar('\n'); break;
            }
            continue;
        }
#ifdef CONFIG_BASE_DEBUG
        const char *sstr, *dstr;
        switch (esp_bt_gap_get_cod_srvc(ptr->bt.cod)) {
        case ESP_BT_COD_SRVC_NONE:              sstr = "Invalid"; break;
        case ESP_BT_COD_SRVC_LMTD_DISCOVER:     sstr = "Limited"; break;
        case ESP_BT_COD_SRVC_POSITIONING:       sstr = "Positioning"; break;
        case ESP_BT_COD_SRVC_NETWORKING:        sstr = "Networking"; break;
        case ESP_BT_COD_SRVC_RENDERING:         sstr = "Rendering"; break;
        case ESP_BT_COD_SRVC_CAPTURING:         sstr = "Capturing"; break;
        case ESP_BT_COD_SRVC_OBJ_TRANSFER:      sstr = "ObjTransfer"; break;
        case ESP_BT_COD_SRVC_AUDIO:             sstr = "Audio"; break;
        case ESP_BT_COD_SRVC_TELEPHONY:         sstr = "Telephony"; break;
        case ESP_BT_COD_SRVC_INFORMATION:       sstr = "Information"; break;
        default:                                sstr = "Unknown"; break;
        }
        switch (esp_bt_gap_get_cod_major_dev(ptr->bt.cod)) {
        case ESP_BT_COD_MAJOR_DEV_MISC:         dstr = "Misc"; break;
        case ESP_BT_COD_MAJOR_DEV_COMPUTER:     dstr = "Computer"; break;
        case ESP_BT_COD_MAJOR_DEV_PHONE:        dstr = "Phone"; break;
        case ESP_BT_COD_MAJOR_DEV_LAN_NAP:      dstr = "LAN NAP"; break;
        case ESP_BT_COD_MAJOR_DEV_AV:           dstr = "Audio/Video"; break;
        case ESP_BT_COD_MAJOR_DEV_PERIPHERAL:   dstr = "Peripheral"; break;
        case ESP_BT_COD_MAJOR_DEV_IMAGING:      dstr = "Imaging"; break;
        case ESP_BT_COD_MAJOR_DEV_WEARABLE:     dstr = "Wearable"; break;
        case ESP_BT_COD_MAJOR_DEV_TOY:          dstr = "Toy"; break;
        case ESP_BT_COD_MAJOR_DEV_HEALTH:       dstr = "Health"; break;
        default:                                dstr = "Unknown"; break;
        }
        if (ptr->bt.cod)
            printf("%s %s Minor 0x%02" PRIu32 " Format %" PRIu32,
                    sstr, dstr, esp_bt_gap_get_cod_minor_dev(ptr->bt.cod),
                    esp_bt_gap_get_cod_format_type(ptr->bt.cod));
#else
        if (ptr->bt.cod) printf("0b%s", format_binary(ptr->bt.cod, 16));
#endif
        putchar('\n');
    }
}

#ifdef CONFIG_BT_CLASSIC_ENABLED
static void bt_scan_done(bool verbose) {
    if (getBits(BT_SCAN_BLOCK_BIT)) {
        clearBits(BT_SCAN_BLOCK_BIT);
    } else if (verbose) {
        printf("BT Scan found %d devices\n", scan_rst_num(s_devs));
        scan_print_devinfo(s_devs, -1);
    }
    setBits(BT_SCAN_DONE_BIT);
}

static esp_err_t bt_scan_entry(uint32_t tout_ms, bool verbose) {
    esp_err_t err = ESP_OK;
    setBits(BT_SCAN_BLOCK_BIT); // mute if scan cancelled
    if (!getBits(BT_SCAN_DONE_BIT) && ( err = esp_bt_gap_cancel_discovery() ))
        return err;
    waitBits(BT_SCAN_DONE_BIT, 10); // wait 10ms for previous discovery
    clearBits(BT_SCAN_BLOCK_BIT);

    esp_bt_inq_mode_t mode = ESP_BT_INQ_MODE_GENERAL_INQUIRY;
    if (!tout_ms) return esp_bt_gap_start_discovery(mode, 10, 0); // 12.8s

    setBits(BT_SCAN_BLOCK_BIT);
    int num = tout_ms / 1280;
    num = CONS(num, ESP_BT_GAP_MIN_INQ_LEN, ESP_BT_GAP_MAX_INQ_LEN);
    if (!( err = esp_bt_gap_start_discovery(mode, num, 0) )) {
        if (!waitBits(BT_SCAN_DONE_BIT, num * 1280 * 2)) {
            err = ESP_ERR_TIMEOUT;
        } else {
            bt_scan_done(verbose);
        }
    }
    return err;
}
#endif

#ifdef CONFIG_BT_BLE_ENABLED
static void ble_scan_done(bool verbose) {
    if (getBits(BLE_SCAN_BLOCK_BIT)) {
        clearBits(BLE_SCAN_BLOCK_BIT);
    } else if (verbose) {
        printf("BLE Scan found %d devices\n", scan_rst_num(s_devs));
        scan_print_devinfo(s_devs, -1);
    }
    setBits(BLE_SCAN_DONE_BIT);
}

static esp_err_t ble_scan_entry(uint32_t tout_ms, bool verbose) {
    esp_err_t err = ESP_OK;
    setBits(BLE_SCAN_BLOCK_BIT); // mute if scan cancelled
    if (!getBits(BLE_SCAN_DONE_BIT) && ( err = esp_ble_gap_stop_scanning() ))
        return err; // ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT
    waitBits(BLE_SCAN_DONE_BIT, 10); // wait 10ms for previous discovery
    clearBits(BLE_SCAN_BLOCK_BIT);

    static const esp_ble_scan_params_t scan_params = {
        .scan_type          = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval      = 0x50, // 50ms
        .scan_window        = 0x30, // 30ms
        .scan_duplicate     = BLE_SCAN_DUPLICATE_ENABLE,
    };
    err = esp_ble_gap_set_scan_params((esp_ble_scan_params_t *)&scan_params);
    if (err) return err;
    waitBits(BLE_SCAN_BLOCK_BIT, 50);
    if (!tout_ms) return esp_ble_gap_start_scanning(10); // 10s

    setBits(BLE_SCAN_BLOCK_BIT);
    int sec = MIN(tout_ms / 1000, 50);
    if (!( err = esp_ble_gap_start_scanning(sec) )) {
        if (!waitBits(BLE_SCAN_DONE_BIT, sec * 2000)) {
            err = ESP_ERR_TIMEOUT;
        } else {
            ble_scan_done(verbose);
        }
    }
    return err;
}
#endif

/*
 * BT & BLE GAP callback
 */

#ifdef CONFIG_BT_CLASSIC_ENABLED
static void bt_gap_cb(
    esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param
) {
    static const char * T = "BT GAP";
    if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGD(T, "Scan started");
        } else {
            ESP_LOGD(T, "Scan stopped");
            bt_scan_done(true);
        }
    } else if (event == ESP_BT_GAP_DISC_RES_EVT) {
        scan_rst_t *dev, *ptr = btmode_find_device(NULL, param->disc_res.bda);
        if (!( dev = ptr ?: calloc(1, sizeof(scan_rst_t)) )) {
            ESP_LOGE(T, "Falled to allocate memory for scan result");
            return;
        }
        memcpy(dev->addr, param->disc_res.bda, sizeof(dev->addr));
        dev->dev_type = ESP_BT_DEVICE_TYPE_BREDR;
        LOOPN(i, param->disc_res.num_prop) {
            esp_bt_gap_dev_prop_t *prop = param->disc_res.prop + i;
            if (prop->type == ESP_BT_GAP_DEV_PROP_COD) {
                dev->bt.cod = *(uint32_t *)prop->val;
            } else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI) {
                dev->rssi = *(uint8_t *)prop->val;
            } else if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME && prop->len) {
                size_t len = MIN(prop->len, sizeof(dev->name) - 1);
                memcpy(dev->name, prop->val, len);
                dev->name[len] = '\0';
            } else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR && prop->len) {
                esp_bt_uuid_t *uuid = &dev->bt.uuid;
                uint8_t len, *data;
#   define EIR_DATA(type, size)                                             \
                ({                                                          \
                    data = esp_bt_gap_resolve_eir_data(                     \
                        prop->val, ESP_BT_EIR_TYPE_CMPL_##type, &len);      \
                    if (!data) data = esp_bt_gap_resolve_eir_data(          \
                        prop->val, ESP_BT_EIR_TYPE_CMPL_##type - 1, &len);  \
                    data && (size ? size == len : len > 0);                 \
                })
                if (EIR_DATA(LOCAL_NAME, 0)) {
                    len = MIN(len, sizeof(dev->name) - 1);
                    memcpy(dev->name, data, len);
                    dev->name[len] = '\0';
                }
                if (EIR_DATA(16BITS_UUID, ESP_UUID_LEN_16)) {
                    memcpy(&uuid->uuid.uuid16, data, uuid->len = len);
                } else if (EIR_DATA(32BITS_UUID, ESP_UUID_LEN_32)) {
                    memcpy(&uuid->uuid.uuid32, data, uuid->len = len);
                } else if (EIR_DATA(128BITS_UUID, ESP_UUID_LEN_128)) {
                    memcpy(uuid->uuid.uuid128, data, uuid->len = len);
                }
#   undef EIR_DATA
            }
        }
        if (ptr != dev) {
            if (!( ptr = s_devs )) {
                s_devs = dev;
            } else {
                while (ptr->next) { ptr = ptr->next; }
                ptr->next = dev;
            }
            esp_bt_gap_get_remote_services(dev->addr);
        }
    } else if (event == ESP_BT_GAP_RMT_SRVCS_EVT) {
        if (param->rmt_srvcs.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(T, BDASTR " found %d service",
                     BDA2STR(param->rmt_srvcs.bda), param->rmt_srvcs.num_uuids);
            LOOPN(i, param->rmt_srvcs.num_uuids) {
                printf("%2d: %s", i, uuid_str(param->rmt_srvcs.uuid_list + i));
            }
            return;
        }
        ESP_LOGE(T, BDASTR " no service found", BDA2STR(param->rmt_srvcs.bda));
    } else if (event == ESP_BT_GAP_AUTH_CMPL_EVT) {
        if (param->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(T, BDASTR " auth failed", BDA2STR(param->auth_cmpl.bda));
            return;
        }
        ESP_LOGI(T, BDASTR " auth success: %s",
                 BDA2STR(param->auth_cmpl.bda), param->auth_cmpl.device_name);
    } else if (event == ESP_BT_GAP_PIN_REQ_EVT) {
        ESP_LOGI(T, BDASTR " request pair code", BDA2STR(param->pin_req.bda));
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(T, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = { 0 };
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(T, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code = { '1', '2', '3', '4' };
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
#   ifdef CONFIG_BT_SSP_ENABLED
    } else if (event == ESP_BT_GAP_CFM_REQ_EVT) {           // ESP_IO_CAP_IO
        ESP_LOGI(T, BDASTR " confirm request: %d",
                 BDA2STR(param->cfm_req.bda), param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
    } else if (event == ESP_BT_GAP_KEY_NOTIF_EVT) {         // ESP_IO_CAP_OUT
        ESP_LOGI(T, BDASTR " notify passkey: %d",
                 BDA2STR(param->key_notif.bda), param->key_notif.passkey);
    } else if (event == ESP_BT_GAP_KEY_REQ_EVT) {           // ESP_IO_CAP_IN
        ESP_LOGI(T, BDASTR " enter passkey", BDA2STR(param->key_req.bda));
        // esp_bt_gap_ssp_passkey_rely(param->key_req.bda, true, 123456);
#   endif
    } else if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT) {
        if (param->read_rmt_name.stat != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(T, BDASTR " read remote name failed",
                     BDA2STR(param->read_rmt_name.bda));
            return;
        }
        if (!memcmp(ctx.addr, param->read_rmt_name.bda, sizeof(ctx.addr))) {
            uint8_t len = sizeof(ctx.name) - 1;
            memcpy(ctx.name, param->read_rmt_name.rmt_name, len);
            ctx.name[len] = '\0';
            ESP_LOGI(T, BDASTR " connected (%s)",
                     BDA2STR(param->read_rmt_name.bda), ctx.name);
        } else {
            ESP_LOGI(T, "Name of " BDASTR " is `%s`",
                     BDA2STR(param->read_rmt_name.bda),
                     param->read_rmt_name.rmt_name);
        }
    } else if (event == ESP_BT_GAP_MODE_CHG_EVT) {
#   ifdef CONFIG_BASE_DEBUG
        const char * str;
        switch (param->mode_chg.mode) {
        case ESP_BT_PM_MD_ACTIVE:   str = "Active"; break;
        case ESP_BT_PM_MD_HOLD:     str = "Hold"; break;
        case ESP_BT_PM_MD_SNIFF:    str = "Sniff"; break;
        case ESP_BT_PM_MD_PARK:     str = "Park"; break;
        default:                    str = "Unknown"; break;
        }
        ESP_LOGI(T, "Mode changed to %s", str);
#   else
        ESP_LOGI(T, "Mode changed to %d", param->mode_chg.mode);
#   endif
    } else {
#   ifdef CONFIG_BASE_DEBUG
        const char * str;
        switch (event) {
        case ESP_BT_GAP_RMT_SRVC_REC_EVT:       str = "RMT_SRVC_REC"; break;
        case ESP_BT_GAP_READ_RSSI_DELTA_EVT:    str = "READ_RSSI_DELTA"; break;
        case ESP_BT_GAP_CONFIG_EIR_DATA_EVT:    str = "CONFIG_EIR_DATA"; break;
        case ESP_BT_GAP_SET_AFH_CHANNELS_EVT:   str = "SET_AFH_CHANNEL"; break;
        case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT: str = "RM_DEV"; break;
        case ESP_BT_GAP_QOS_CMPL_EVT:           str = "QOS_CMPL"; break;
        default:                                str = "Unknown"; break;
        }
        ESP_LOGI(T, "Unhandled event %s", str);
#   else
        ESP_LOGD(T, "Unhandled event %d", event);
#   endif
    }
}
#endif // CONFIG_BT_CLASSIC_ENABLED

#ifdef CONFIG_BT_BLE_ENABLED
static void ble_gap_cb(
    esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param
) {
    static const char * T = "BLE GAP";
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        ESP_LOGD(T, "Scan param updated");
        setBits(BLE_SCAN_BLOCK_BIT);
    } else if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT) {
        ESP_LOGD(T, "Scan started");
    } else if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) {
        ESP_LOGD(T, "Scan stopped");
        ble_scan_done(true);
    } else if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        switch (param->scan_rst.search_evt) {
        case ESP_GAP_SEARCH_INQ_CMPL_EVT: FALLTH;
        case ESP_GAP_SEARCH_SEARCH_CANCEL_CMPL_EVT:
            ESP_LOGD(T, "Scan timeout");
            ble_scan_done(true);
            break;
#   ifdef CONFIG_BASE_DEBUG
        case ESP_GAP_SEARCH_DISC_RES_EVT:       ESP_LOGD(T, "DISC_RES"); break;
        case ESP_GAP_SEARCH_DISC_BLE_RES_EVT:   ESP_LOGD(T, "DISC_BLE"); break;
        case ESP_GAP_SEARCH_DISC_CMPL_EVT:      ESP_LOGD(T, "DISC_CMPL"); break;
        case ESP_GAP_SEARCH_DI_DISC_CMPL_EVT:   ESP_LOGD(T, "DI_CMPL"); break;
        case ESP_GAP_SEARCH_INQ_DISCARD_NUM_EVT: ESP_LOGD(T, "INQ_DIS"); break;
#   endif
        default: break;
        }
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;
        scan_rst_t *dev, *ptr = btmode_find_device(NULL, param->scan_rst.bda);
        if (!( dev = ptr ?: calloc(1, sizeof(scan_rst_t)) )) {
            ESP_LOGE(T, "Falled to allocate memory for scan result");
            return;
        }
        memcpy(dev->addr, param->scan_rst.bda, sizeof(dev->addr));
        dev->rssi = param->scan_rst.rssi;
        dev->dev_type = param->scan_rst.dev_type;
        dev->ble.addr_type = param->scan_rst.ble_addr_type;
        uint8_t len, *data;
#   define ADV_DATA(type, size)                                             \
        ({                                                                  \
            data = esp_ble_resolve_adv_data(                                \
                param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_##type, &len);     \
            if (!data) data = esp_ble_resolve_adv_data(                     \
                param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_##type - 1, &len); \
            data && (size ? size == len : len > 0);                         \
        })
        if (ADV_DATA(NAME_CMPL, 0)) {
            len = MIN(len, sizeof(dev->name) - 1);
            memcpy(dev->name, data, len);
            dev->name[len] = '\0';
        }
        if (ADV_DATA(16SRV_CMPL, sizeof(dev->ble.gatts_uuid)))
            memcpy(&dev->ble.gatts_uuid, data, len);
        if (ADV_DATA(APPEARANCE, sizeof(dev->ble.appearance)))
            memcpy(&dev->ble.appearance, data, len);
#   undef ADV_DATA
        if (ptr != dev) {
            if (!( ptr = s_devs )) {
                s_devs = dev;
            } else {
                while (ptr->next) { ptr = ptr->next; }
                ptr->next = dev;
            }
        }
    } else if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
        ESP_LOGD(T, "Advitising data updated");
    } else if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
        ESP_LOGD(T, "Advitising started");
    } else if (event == ESP_GAP_BLE_AUTH_CMPL_EVT) {
        /* Error on windows: connect and disconnect alternate endlessly:
         *  I (08:00:03.560) BLE HIDD: Connected
         *  E (4909) BT_BTM: Device not found
         *  I (08:00:03.641) BLE HIDD: Disconnected
         *  E (08:00:03.645) BLE GAP: b4:ae:2b:cc:84:9f auth failed
         *  I (08:00:04.337) BLE HIDD: Connected
         *  E (5618) BT_BTM: Device not found
         *  I (08:00:04.351) BLE HIDD: Disconnected
         *  E (08:00:04.359) BLE GAP: b4:ae:2b:cc:84:9f auth failed
         * It can be fixed temporarily by "unpair/forget device and re-connect"
         */
        esp_ble_auth_cmpl_t *auth = &param->ble_security.auth_cmpl;
        if (!auth->success) {
            ESP_LOGE(T, BDASTR " auth failed", BDA2STR(auth->bd_addr));
            esp_ble_remove_bond_device(auth->bd_addr);
            esp_ble_gap_disconnect(auth->bd_addr);
        } else {
            ESP_LOGI(T, BDASTR " auth success", BDA2STR(auth->bd_addr));
            ctx.enabled = true;
            memcpy(ctx.addr, auth->bd_addr, sizeof(ctx.addr));
        }
    } else if (event == ESP_GAP_BLE_KEY_EVT) {
        esp_ble_key_t *key = &param->ble_security.ble_key;
#   ifdef CONFIG_BASE_DEBUG
        const char *kstr;
        switch (key->key_type) {
        case ESP_LE_KEY_NONE:   kstr = "NONE"; break;
        case ESP_LE_KEY_PENC:   kstr = "PENC"; break;
        case ESP_LE_KEY_PID:    kstr = "PID"; break;
        case ESP_LE_KEY_PCSRK:  kstr = "PCSRK"; break;
        case ESP_LE_KEY_PLK:    kstr = "PLK"; break;
        case ESP_LE_KEY_LENC:   kstr = "LENC"; break;
        case ESP_LE_KEY_LID:    kstr = "LID"; break;
        case ESP_LE_KEY_LCSRK:  kstr = "LCSRK"; break;
        case ESP_LE_KEY_LLK:    kstr = "LLK"; break;
        default:                kstr = "Unknown"; break;
        }
        ESP_LOGI(T, BDASTR " key type %s", BDA2STR(key->bd_addr), kstr);
#   else
        ESP_LOGI(T, BDASTR " key type 0b%s",
                 BDA2STR(key->bd_addr), format_binary(key->key_type, 8));
#   endif
    } else if (event == ESP_GAP_BLE_NC_REQ_EVT) {           // ESP_IO_CAP_IO
        esp_ble_sec_key_notif_t *key = &param->ble_security.key_notif;
        ESP_LOGI(T, BDASTR " confirm passkey: %" PRIu32,
                 BDA2STR(key->bd_addr), key->passkey);
        esp_ble_confirm_reply(key->bd_addr, true);
    } else if (event == ESP_GAP_BLE_PASSKEY_NOTIF_EVT) {    // ESP_IO_CAP_OUT
        esp_ble_sec_key_notif_t *key = &param->ble_security.key_notif;
        ESP_LOGI(T, BDASTR " notify passkey: %" PRIu32,
                 BDA2STR(key->bd_addr), key->passkey);
    } else if (event == ESP_GAP_BLE_PASSKEY_REQ_EVT) {      // ESP_IO_CAP_IN
        esp_ble_sec_req_t *req = &param->ble_security.ble_req;
        ESP_LOGI(T, BDASTR " enter passkey", BDA2STR(req->bd_addr));
        // esp_ble_passkey_reply(req->bd_addr, true, 123456);
    } else if (event == ESP_GAP_BLE_SEC_REQ_EVT) {
        esp_ble_sec_req_t *req = &param->ble_security.ble_req;
        ESP_LOGI(T, BDASTR " security request", BDA2STR(req->bd_addr));
        esp_ble_gap_security_rsp(req->bd_addr, true);
    } else {
        ESP_LOGD(T, "Unhandled event %d", event);
    }
}
#endif // CONFIG_BT_BLE_ENABLED

#ifdef IDF_TARGET_V4
#   define esp_bt_gap_set_device_name esp_bt_dev_set_device_name
#endif

esp_err_t bt_common_init(esp_bt_mode_t mode, bool clean) {
    esp_err_t err = ESP_OK;
    if (BT_ENABLED()) return err;

    if (!ctx.evtgrp) {
        ctx.evtgrp = xEventGroupCreate();
        setBits(BT_SCAN_DONE_BIT | BLE_SCAN_DONE_BIT);
        ctx.cmode = ESP_BT_CONNECTABLE;
        ctx.dmode = strtob(Config.sys.BT_SCAN)
                    ? ESP_BT_GENERAL_DISCOVERABLE
                    : ESP_BT_NON_DISCOVERABLE;
    }

    if (BT_IDLE() && clean) {
        if (mode == ESP_BT_MODE_CLASSIC_BT) {
            err = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        } else if (mode == ESP_BT_MODE_BLE) {
            err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        }
    }

    char name[32];
    bool gmpad_tricks = HIDTool.pad && HIDTool.pad != GMPAD_GENERAL;
    snprintf(name, sizeof(name), "%s-%s", Config.info.NAME, Config.info.UID);
    if (gmpad_tricks) {
        snprintf(name, sizeof(name), "%s-%s",
                 Config.info.NAME, Config.app.HID_MODE);
#ifndef IDF_TARGET_V4
        uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_BT);
        mac[1] += HIDTool.pad;      // set different MAC for difference layout
        if (!err) err = esp_iface_mac_addr_set(mac, ESP_MAC_BT);
        if (!err) ESP_LOGI("BTMode", "Using custom MAC: " MACSTR, MAC2STR(mac));
#endif
    }

    esp_bt_controller_config_t conf = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
#ifdef CONFIG_IDF_TARGET_ESP32
    conf.mode = mode;               // ignore CONFIG_BTDM_CTRL_MODE_XXX
#endif
#ifdef CONFIG_BT_CLASSIC_ENABLED
    if (HAS_BT(mode)) {
        conf.bt_max_acl_conn = 3;   // ignore CONFIG_BTDM_CTRL_MODE_XXX
        conf.bt_max_sync_conn = 3;
    }
#endif
    if (!err) err = esp_bt_controller_init(&conf);
    if (!err) err = esp_bt_controller_enable(mode);
    if (!err) err = esp_bluedroid_init();
    if (!err) err = esp_bluedroid_enable();

#ifdef CONFIG_BT_CLASSIC_ENABLED
    if (HAS_BT(mode)) {
        if (!err) err = esp_bt_gap_set_device_name(name);
        if (!err) err = esp_bt_gap_register_callback(bt_gap_cb);
        if (!err) err = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, NULL);
        if (!err) err = esp_bt_gap_set_scan_mode(ctx.cmode, ctx.dmode);
#   ifdef CONFIG_BT_SSP_ENABLED
        esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
        esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap, 1);
#   endif
    }
#endif
#ifdef CONFIG_BT_BLE_ENABLED
    if (HAS_BLE(mode)) {
        if (!err) err = esp_ble_gap_set_device_name(name);
        if (!err) err = esp_ble_gap_register_callback(ble_gap_cb);
        uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
        const struct {
            esp_ble_sm_param_t type;
            uint8_t value;
        } params[] = {
            { ESP_BLE_SM_AUTHEN_REQ_MODE,   ESP_LE_AUTH_REQ_SC_MITM_BOND },
            { ESP_BLE_SM_IOCAP_MODE,        ESP_IO_CAP_IO },
            { ESP_BLE_SM_MAX_KEY_SIZE,      16 },
            { ESP_BLE_SM_SET_INIT_KEY,      keys },
            { ESP_BLE_SM_SET_RSP_KEY,       keys },
        };
        LOOPN(i, err ? 0 : LEN(params)) {
            err = esp_ble_gap_set_security_param(
                params[i].type, (void *)&params[i].value, sizeof(uint8_t));
        }
        if (!err) {
            uint32_t passkey = 1234; // ESP_IO_CAP_OUT
            err = esp_ble_gap_set_security_param(
                ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(passkey));
        }
    }
#endif
    ctx.mode = err ? 0 : mode;
    return err;
}

esp_err_t bt_common_exit(bool clean) {
    esp_err_t err = ESP_OK;
    if (BT_IDLE()) return err;
    if (!err) err = esp_bluedroid_disable();
    if (!err) err = esp_bluedroid_deinit();
    if (!err) err = esp_bt_controller_disable();
    if (!err) err = esp_bt_controller_deinit();
    if (!err && clean) {
        err = esp_bt_mem_release(ESP_BT_MODE_BTDM);
        if (!err) return ESP_FAIL; // need reboot
    }
    ctx.mode = 0;
    return err;
}

/*
 * BT HID Device
 */

#ifdef CONFIG_BASE_BT_HID_DEVICE

#   define SCAN_ENABLE() \
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE)
#   define SCAN_DISABLE() \
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)

static const char * BT = "BT HIDD";

static bool bt_check_status(esp_hidd_status_t status, const char * msg) {
    if (status == ESP_HIDD_SUCCESS) return true;
    if (msg) {
#   ifdef CONFIG_BASE_DEBUG
        const char *str;
        switch (status) {
        case ESP_HIDD_NO_RES:           str = "NO_RES"; break;
        case ESP_HIDD_BUSY:             str = "BUSY"; break;
        case ESP_HIDD_NO_DATA:          str = "NO_DATA"; break;
        case ESP_HIDD_NEED_INIT:        str = "NEED_INIT"; break;
        case ESP_HIDD_NEED_DEINIT:      str = "NEED_DEINIT"; break;
        case ESP_HIDD_NEED_REG:         str = "NEED_REG"; break;
        case ESP_HIDD_NEED_DEREG:       str = "NEED_DEREG"; break;
        case ESP_HIDD_NO_CONNECTION:    str = "NO_CONNECTION"; break;
        default:                        str = "Unknown"; break;
        }
        ESP_LOGE(BT, "%s failed: %s", msg, str);
#   else
        ESP_LOGE(BT, "%s failed: %d", msg, status);
#   endif
    }
    return false;
}

static void bt_hidd_cb(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param) {
    if (event == ESP_HIDD_INIT_EVT) {
        if (!bt_check_status(param->init.status, "Init hidd")) return;
        ctx.app = (esp_hidd_app_param_t) {
            .name           = "BT HID Device",
            .description    = HIDTool.dstr,
            .subclass       = ESP_HID_CLASS_COM,
            .provider       = HIDTool.vendor,
            .desc_list      = HIDTool.desc,
            .desc_list_len  = HIDTool.dlen,
        };
        esp_bt_hid_device_register_app(&ctx.app, &ctx.qos, &ctx.qos);
    } else if (event == ESP_HIDD_REGISTER_APP_EVT) {
        if (!bt_check_status(param->register_app.status, "Register")) return;
        SCAN_ENABLE();
        ctx.enabled = true;
        if (param->register_app.in_use && param->register_app.bd_addr) {
            ESP_LOGI(BT, "Start virtual cable plug");
            esp_bt_hid_device_connect(param->register_app.bd_addr);
        }
    } else if (event == ESP_HIDD_UNREGISTER_APP_EVT) {
        if (!bt_check_status(param->unregister_app.status, "Unregister")) return;
        esp_bt_gap_set_scan_mode(ctx.cmode, ctx.dmode);
        ctx.enabled = false;
    } else if (event == ESP_HIDD_OPEN_EVT) {
        if (!bt_check_status(param->open.status, "Open")) return;
        if (param->open.conn_status == ESP_HIDD_CONN_STATE_CONNECTING) {
            ESP_LOGI(BT, "Connecting...");
            return;
        }
        if (param->open.conn_status != ESP_HIDD_CONN_STATE_CONNECTED) return;
        if (esp_bt_gap_read_remote_name(param->open.bd_addr))
            ESP_LOGI(BT, BDASTR " connected", BDA2STR(param->open.bd_addr));
        memcpy(ctx.addr, param->open.bd_addr, sizeof(ctx.addr));
        ctx.connected = true;
        SCAN_DISABLE();
    } else if (event == ESP_HIDD_CLOSE_EVT) {
        if (!bt_check_status(param->close.status, "Close")) return;
        if (param->close.conn_status == ESP_HIDD_CONN_STATE_DISCONNECTING) {
            ESP_LOGI(BT, "Disconnecting...");
        }
        if (param->close.conn_status == ESP_HIDD_CONN_STATE_DISCONNECTED) {
            if (strlen(ctx.name ?: "")) {
                ESP_LOGI(BT, BDASTR " disconnected (%s)",
                         BDA2STR(ctx.addr), ctx.name);
            } else {
                ESP_LOGI(BT, BDASTR " disconnected", BDA2STR(ctx.addr));
            }
            ctx.connected = false;
            memset(ctx.name, 0, sizeof(ctx.name));
            memset(ctx.addr, 0, sizeof(ctx.addr));
            SCAN_ENABLE();
        }
    } else if (event == ESP_HIDD_SEND_REPORT_EVT) {
        if (bt_check_status(param->send_report.status, NULL)) return;
        ESP_LOGE(BT, "Send report id 0x%02X type %d status %d reason %d",
                 param->send_report.report_id, param->send_report.report_type,
                 param->send_report.status, param->send_report.reason);
    } else if (event == ESP_HIDD_GET_REPORT_EVT) {
        ESP_LOGI(BT, "Get report id 0x%02X type %d size %d",
                 param->get_report.report_id, param->get_report.report_type,
                 param->get_report.buffer_size);
        if (param->get_report.report_type != ESP_HIDD_REPORT_TYPE_INPUT) {
            esp_bt_hid_device_report_error(
                    ESP_HID_PAR_HANDSHAKE_RSP_ERR_INVALID_PARAM);
        } else {
            esp_bt_hid_device_report_error(
                    ESP_HID_PAR_HANDSHAKE_RSP_ERR_UNSUPPORTED_REQ);
            /* Maybe we shouldn't reply to GET_REPORT event?
             * Just sending zeros to the HID host.
            uint8_t rid = param->get_report.report_id;
            if (0 < rid && rid < REPORT_ID_MAX) {
                uint8_t size = HIDTool.rlen[rid];
                uint8_t buf[size];
                memset(buf, 0, size);
            }
            if (esp_bt_hid_device_send_report(
                    ESP_HIDD_REPORT_TYPE_INPUT, rid, size, buf))
                esp_bt_hid_device_report_error(
                        ESP_HID_PAR_HANDSHAKE_RSP_ERR_INVALID_REP_ID);
            */
        }
    } else if (event == ESP_HIDD_SET_PROTOCOL_EVT) {
        ESP_LOGI(BT, "Protocol set to %s",
                 param->set_protocol.protocol_mode ? "REPORT" : "BOOT");
    } else if (event == ESP_HIDD_VC_UNPLUG_EVT) {
        if (!bt_check_status(param->vc_unplug.status, "VC Unplug")) return;
        if (param->close.conn_status == ESP_HIDD_CONN_STATE_DISCONNECTED) {
            ESP_LOGI(BT, "Disconnected");
            SCAN_ENABLE();
        }
    } else {
        ESP_LOGD(BT, "Unhandled event %d", event);
    }
}

esp_err_t bt_hidd_init(btmode_t prev) {
    esp_err_t err = bt_common_init(ESP_BT_MODE_CLASSIC_BT, !ISBT(prev));
    esp_bt_cod_t cod = { .major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL };
    if (!err) err = esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);
    if (!err) err = esp_bt_hid_device_register_callback(bt_hidd_cb);
    if (!err) err = esp_bt_hid_device_init();
    // Do NOT use esp_hidd_dev_init(..., ESP_HID_TRANSPORT_BT, ...)
    return err;
}

esp_err_t bt_hidd_exit(btmode_t next) {
    esp_err_t err = esp_bt_hid_device_deinit();
    if (!err && !ISBT(next)) err = bt_common_exit(true);
    return err;
}

#else

esp_err_t bt_hidd_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t bt_hidd_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_BT_HID_DEVICE

/*
 * BLE HID Device
 */

#ifdef CONFIG_BASE_BLE_HID_DEVICE

#   define ADV_ENABLE()     esp_ble_gap_start_advertising(&hidd_adv_params)
#   define ADV_DISABLE()    esp_ble_gap_stop_advertising()

static const char * BLE = "BLE HIDD";

static const uint8_t hidd_service_uuid128[] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t ble_adv_data = {
    .set_scan_rsp           = false,
    .include_name           = true,
    .include_txpower        = true,
    .min_interval           = 0x0006, // 7.5ms
    .max_interval           = 0x0010, // 20ms
    .appearance             = ESP_HID_APPEARANCE_GENERIC, // 0x03C0
    .manufacturer_len       = 0,
    .p_manufacturer_data    = NULL,
    .service_data_len       = 0,
    .p_service_data         = NULL,
    .service_uuid_len       = sizeof(hidd_service_uuid128),
    .p_service_uuid         = (uint8_t *)hidd_service_uuid128,
    .flag                   = ESP_BLE_ADV_FLAG_GEN_DISC
                            | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT, // 0x6
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20, // 20ms
    .adv_int_max        = 0x30, // 30ms
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static bool ble_check_status(esp_err_t status, const char * msg) {
    if (status == ESP_OK) return true;
    if (msg) ESP_LOGE(BLE, "%s failed: %s", msg, esp_err_to_name(status));
    return false;
}

static void ble_hidd_cb(void *a, esp_event_base_t b, int32_t id, void *data) {
    esp_hidd_event_data_t *param = data;
    if (id == ESP_HIDD_START_EVENT) {
        ADV_ENABLE();
        ctx.enabled = true;
    } else if (id == ESP_HIDD_CONNECT_EVENT) {
        if (!ble_check_status(param->connect.status, "Connect")) return;
        ESP_LOGI(BLE, "Connected");
        ctx.connected = true;
        ADV_DISABLE();
    } else if (id == ESP_HIDD_PROTOCOL_MODE_EVENT) {
        ESP_LOGI(BLE, "Protocol set to %s",
                 param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
    } else if (id == ESP_HIDD_OUTPUT_EVENT) {
        if (param->output.usage == ESP_HID_USAGE_KEYBOARD &&
            param->output.report_id == REPORT_ID_KEYBD &&
            param->output.length == sizeof(hid_keybd_output_t)
        ) {
            hid_keybd_output_t *leds = (void *)param->output.data;
            ESP_LOGI(BLE, "KEYBD Kana %d, Compose %d, "
                          "ScrollLock %d, CapsLock %d, NumLock %d",
                    leds->kana, leds->compose,
                    leds->scrolllock, leds->capslock, leds->numlock);
        } else if (
            param->output.usage == ESP_HID_USAGE_GENERIC &&
            param->output.report_id == (REPORT_ID_GMPAD << 4) &&
            param->output.length == sizeof(hid_gmpad_output_xinput_t)
        ) {
            hid_gmpad_output_xinput_t *act = (void *)param->output.data;
            ESP_LOGD(BLE, "GMPAD ACT %d %3d-%-3d %3d~%-3d LOOP %d S%dms D%dms",
                    act->enabled, act->mag_left, act->mag_right,
                    act->mag_weak, act->mag_strong, act->loop_count,
                    act->start_delay * 10, act->duration * 10);
        } else {
            ESP_LOGD(BLE, "Output for %s REPORT_ID=%d SIZE=%d",
                    esp_hid_usage_str(param->output.usage),
                    param->output.report_id, param->output.length);
        }
    } else if (id == ESP_HIDD_DISCONNECT_EVENT) {
        if (!ble_check_status(param->disconnect.status, "Disconnect")) return;
        ESP_LOGI(BLE, "Disconnected");
        ctx.connected = false;
        ADV_ENABLE();
    } else if (id == ESP_HIDD_STOP_EVENT) {
        if (ctx.dmode) {
            ADV_ENABLE();
        } else {
            ADV_DISABLE();
        }
        ctx.enabled = false;
    }
}

esp_err_t ble_hidd_init(btmode_t prev) {
    esp_err_t err = bt_common_init(ESP_BT_MODE_BLE, !ISBLE(prev));
    esp_hid_raw_report_map_t ble_report_maps[] = {
        { .data = HIDTool.desc, .len = HIDTool.dlen }
    };
    const esp_hid_device_config_t ble_hidd_conf = {
        .vendor_id          = HIDTool.vid,
        .product_id         = HIDTool.pid,
        .version            = HIDTool.ver,
        .manufacturer_name  = HIDTool.vendor,
        .serial_number      = HIDTool.serial,
        .report_maps        = ble_report_maps,
        .report_maps_len    = LEN(ble_report_maps),
    };
    if (HIDTool.pad && HIDTool.pad != GMPAD_GENERAL)
        ble_adv_data.appearance = ESP_HID_APPEARANCE_GAMEPAD;
    if (!err) err = esp_ble_gap_config_adv_data(&ble_adv_data);
    if (!err) err = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
    if (!err) err = esp_hidd_dev_init(&ble_hidd_conf,
                                      ESP_HID_TRANSPORT_BLE,
                                      ble_hidd_cb, &ctx.hiddev);
    return err;
}

esp_err_t ble_hidd_exit(btmode_t next) {
    esp_err_t err = esp_hidd_dev_deinit(ctx.hiddev);
    if (!err && !ISBLE(next)) err = bt_common_exit(true);
    return err;
}

#else

esp_err_t ble_hidd_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t ble_hidd_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_BLE_HID_DEVICE

bool hidb_send_report(const hid_report_t *rpt) {
    bool sent = false;
    if (!ctx.enabled || !ctx.connected || !HID_VALID_REPORT(rpt)) return sent;
#   ifdef CONFIG_BASE_BT_HID_DEVICE
    if (HAS_BT(ctx.mode))
        sent |= !esp_bt_hid_device_send_report(
            ESP_HIDD_REPORT_TYPE_INTRDATA, rpt->id, rpt->size, (uint8_t *)rpt);
#   endif
#   ifdef CONFIG_BASE_BLE_HID_DEVICE
    if (HAS_BLE(ctx.mode))
        sent |= !esp_hidd_dev_input_set(
            ctx.hiddev, 0, rpt->id, (uint8_t *)rpt, rpt->size);
#   endif
    return sent;
}

esp_err_t btmode_scan(uint32_t timeout_ms) {
    esp_err_t err = ESP_OK;
#   ifdef CONFIG_BT_CLASSIC_ENABLED
    if (!err && HAS_BT(ctx.mode)) err = bt_scan_entry(timeout_ms, true);
#   endif
#   ifdef CONFIG_BT_BLE_ENABLED
    if (!err && HAS_BLE(ctx.mode)) err = ble_scan_entry(timeout_ms, true);
#   endif
    return err;
}

esp_err_t btmode_config(bool c, bool d) {
    esp_err_t err = ESP_OK;
    esp_bt_connection_mode_t cmode = \
        c ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE;
    esp_bt_discovery_mode_t dmode = \
        d ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE;
    if (ctx.cmode == cmode && ctx.dmode == dmode) return err;
    ctx.cmode = cmode;
    ctx.dmode = dmode;
    config_set("sys.bt.scan", d ? "1" : "0");
#   ifdef CONFIG_BT_CLASSIC_ENABLED
    if (HAS_BT(ctx.mode)) err = esp_bt_gap_set_scan_mode(cmode, dmode);
#   endif
#   ifdef CONFIG_BT_BLE_ENABLED
    if (HAS_BLE(ctx.mode)) {
        if (d) {
            ADV_ENABLE();
        } else {
            ADV_DISABLE();
        }
    }
#   endif
    return err;
}

esp_err_t btmode_battery(uint8_t pcent) {
#   ifdef CONFIG_BT_BLE_ENABLED
    if (!HAS_BLE(ctx.mode) || !ctx.enabled || !ctx.connected)
        return ESP_ERR_INVALID_STATE;
    return esp_hidd_dev_battery_set(ctx.hiddev, pcent);
#   else
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(pcent);
#   endif
}

esp_err_t btmode_connect(const char *name, uint8_t *bda) {
    scan_rst_t *dev = btmode_find_device(name, bda);
    if (!dev) return ESP_ERR_NOT_FOUND;
    if (dev->dev_type == ESP_BT_DEVICE_TYPE_BLE &&
        dev->ble.gatts_uuid && dev->ble.gatts_uuid != ESP_GATT_UUID_HID_SVC)
        return ESP_ERR_INVALID_ARG;
    return bthost_connect(dev->addr, dev->dev_type, dev->ble.addr_type);
}

#else

esp_err_t btmode_scan(uint32_t t) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(t);
}
esp_err_t btmode_config(bool c, bool d) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(c); NOTUSED(d);
}
esp_err_t btmode_battery(uint8_t p) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(p);
}
esp_err_t btmode_connect(const char *n, uint8_t *b) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(n); NOTUSED(b);
}

#endif // CONFIG_BASE_USE_BT
