/*
 * File: usbmoded.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024-03-25 18:26:20
 */

#include "usbmode.h"
#include "filesys.h"
#include "config.h"

#ifdef CONFIG_USE_USB

#include "usb/usb_host.h"
#include "tinyusb.h"

#ifdef CONFIG_USB_CDC_DEVICE
#   include "tusb_cdc_acm.h"
#endif

#ifdef CONFIG_USB_MSC_DEVICE
#   ifdef TARGET_IDF_5
#       include "tusb_msc_storage.h"        // idf add-dependency esp_tinyusb
#   else
#       include "sdmmc_cmd.h"
#   endif
#endif

#ifdef CONFIG_USB_HID_DEVICE
#   ifdef TARGET_IDF_5
#       include "class/hid/hid_device.h"    // idf add-dependency esp_tinyusb
#   else
#       include "freertos/FreeRTOS.h"
#       include "freertos/task.h"
#       include "freertos/queue.h"
#       include "freertos/semphr.h"
#   endif
#endif

#define NUM_DISK                1 // currently only one endpoint is supported

static const char *TAG = "USBDevice";

static filesys_info_t info[NUM_DISK];
static bool mounted = false, inited = false;
static bool cdc_enabled = false, msc_enabled = false, hid_enabled = false;

/******************************************************************************
 * Helper functions
 */

void usbmoded_status(usbmode_t mode) {
    printf("inited: %s, mounted: %s\n",
            inited ? "true" : "false", mounted ? "true" : "false");
#ifdef CONFIG_USB_CDC_DEVICE
    if (mode == CDC_DEVICE) {
#   ifdef CONFIG_USB_CDC_DEVICE_SERIAL
        puts("Running as CDC serial device");
#   endif
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
        puts("Running as CDC console device");
#   endif
    }
#endif
#ifdef CONFIG_USB_MSC_DEVICE
    if (mode == MSC_DEVICE) {
        LOOPN(i, NUM_DISK) {
            if (info[i].pdrv == FF_DRV_NOT_USED) {
                printf("Disk[%d]: not mouned / supported\n", i);
            } else {
                printf("Disk[%d]: pdrv=%u, ssize=%u, total=%s\n",
                    i, info[i].pdrv, info[i].blksize,
                    format_size(info[i].total, false));
            }
        }
    }
#endif
#ifdef CONFIG_USB_HID_DEVICE
    if (mode == HID_DEVICE) puts("Running as HID keyboard & mouse device");
#endif
}

bool usbmoded_device(const void *arg) {
    const usb_device_desc_t *desc = arg;
    return (
#if CONFIG_TINYUSB_DESC_USE_ESPRESSIF_VID
        desc->idVendor == USB_ESPRESSIF_VID ||
#else
        desc->idVendor == CONFIG_TINYUSB_DESC_CUSTOM_VID ||
#endif
#ifdef CONFIG_USB_CDC_HOST
        desc->bDeviceClass == TUSB_CLASS_CDC ||
#endif
#ifdef CONFIG_USB_MSC_HOST
        desc->bDeviceClass == TUSB_CLASS_MSC ||
#endif
#ifdef CONFIG_USB_HID_HOST
        desc->bDeviceClass == TUSB_CLASS_HID ||
#endif
        (
            desc->bDeviceClass == TUSB_CLASS_MISC &&
            desc->bDeviceSubClass == MISC_SUBCLASS_COMMON &&
            desc->bDeviceProtocol == MISC_PROTOCOL_IAD
        )
    );
}

static bool usbmoded_reconnect() {
    bool ret = tud_disconnect();
    if (ret) {
        msleep(100);
        ret = tud_connect();
    }
    return ret;
}

/******************************************************************************
 * USB Descriptor hacks
 */

// see idf-v4.4/tinyusb/additions/include_private/descriptors_control.h
// see idf-v4.4/tinyusb/additions/src/usb_descriptors.c
// see esp_tinyusb/usb_descriptors.c
// see idf-v4.4-tinyusb-hid.patch

#include "../include_private/usb_descriptors.h"
#ifdef TARGET_IDF_5
#   define desc_dev descriptor_dev_default
#   define desc_str descriptor_str_default
#else
#   define desc_dev descriptor_kconfig
#   define desc_str descriptor_str_kconfig
#endif

#define TUD_HID_REPORT_DESC_DIAL(...) \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                            ,\
    HID_USAGE      ( 0x0e )           ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION )                        ,\
        __VA_ARGS__ \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER )                      ,\
        HID_USAGE ( 0x21 )                                               ,\
        HID_COLLECTION ( HID_COLLECTION_PHYSICAL )                       ,\
            HID_USAGE_PAGE ( HID_USAGE_PAGE_BUTTON )                     ,\
            HID_USAGE ( 1 )                                              ,\
            HID_REPORT_COUNT ( 1                                      )  ,\
            HID_REPORT_SIZE  ( 1                                      )  ,\
            HID_LOGICAL_MIN  ( 0                                      )  ,\
            HID_LOGICAL_MAX  ( 1                                      )  ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )  ,\
            HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                    ,\
            HID_USAGE ( HID_USAGE_DESKTOP_DIAL )                         ,\
            HID_REPORT_COUNT ( 1                                      )  ,\
            HID_REPORT_SIZE  ( 15                                     )  ,\
            HID_UNIT_EXPONENT( 15                                     )  ,\
                HID_UNIT ( 0x14 )                                        ,\
                HID_PHYSICAL_MIN_N ( -3600, 2                         )  ,\
                HID_PHYSICAL_MAX_N ( 3600, 2                          )  ,\
                HID_LOGICAL_MIN_N  ( -3600, 2                         )  ,\
                HID_LOGICAL_MAX_N  ( 3600, 2                          )  ,\
            HID_INPUT     ( HID_DATA | HID_VARIABLE | HID_RELATIVE)      ,\
        HID_COLLECTION_END                                               ,\
    HID_COLLECTION_END \

#ifdef CONFIG_USB_HID_DEVICE
enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE,
    REPORT_ID_DIAL,
};

static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
    TUD_HID_REPORT_DESC_DIAL(HID_REPORT_ID(REPORT_ID_DIAL)),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t i) { // overwrite weak
    return desc_hid_report; NOTUSED(i);
}

static size_t rlen = sizeof(desc_hid_report), blen = CFG_TUD_HID_EP_BUFSIZE;
#else
static size_t rlen = 0, blen = 0;
#endif

static uint8_t const * config_desc() {
    static uint8_t buf[
        TUD_CONFIG_DESC_LEN +
        CFG_TUD_CDC * TUD_CDC_DESC_LEN +
        CFG_TUD_MSC * TUD_MSC_DESC_LEN +
        CFG_TUD_HID * TUD_HID_DESC_LEN
    ];
    size_t total = TUD_CONFIG_DESC_LEN;
    uint8_t itf = 0;
    if (cdc_enabled) {                  // stridx, EPN, size, EPO, EPI, size
        uint8_t c[] = { TUD_CDC_DESCRIPTOR(itf, 4, 0x81, 8, 0x02, 0x82, 64) };
        memcpy(buf + total, c, sizeof(c));
        itf += 2; // for ITF_NUM_CDC & ITF_NUM_CDC_DATA
        total += sizeof(c);
    }
    if (msc_enabled) {                  // stridx, EPO, EPI, size
        uint8_t m[] = { TUD_MSC_DESCRIPTOR(itf, 5, 0x03, 0x83, 64) };
        memcpy(buf + total, m, sizeof(m));
        itf += 1; // for ITF_NUM_MSC
        total += sizeof(m);
    }
    if (hid_enabled) {                  // stridx, proto,   EPI, size, poll
        uint8_t h[] = { TUD_HID_DESCRIPTOR(itf, 6, 0, rlen, 0x84, blen, 10 ) };
        memcpy(buf + total, h, sizeof(h));
        itf += 1; // for ITF_NUM_HID
        total += sizeof(h);
    }
    uint8_t desc[] = {
        TUD_CONFIG_DESCRIPTOR(
            1, itf, 0, total, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500
        )
    };
    memcpy(buf, desc, sizeof(desc));
    return buf;
}

#ifdef TARGET_IDF_4
uint8_t const *tud_descriptor_configuration_cb(uint8_t i) { // overwrite weak
    return config_desc(); NOTUSED(i);
}
#endif

static esp_err_t usbd_common_init() {
    if (inited) return ESP_OK;
    LOOPN(i, NUM_DISK) { info[i].pdrv = FF_DRV_NOT_USED; }
    int ver[3];
    if (parse_all(Config.info.VER, ver, 3) >= 2)
        desc_dev.bcdDevice = (uint8_t)(ver[0] << 8) | (uint8_t)ver[1];
    if (strlen(Config.info.UID)) desc_str[3] = Config.info.UID;

    tinyusb_config_t tusb_conf = {
        .external_phy = false,
#ifdef TARGET_IDF_5
        .device_descriptor = &desc_dev,
        .string_descriptor = desc_str,
        .string_descriptor_count = LEN(desc_str),
        .configuration_descriptor = config_desc(),
#else
        .descriptor = &desc_dev,
        .string_descriptor = desc_str,
#endif
    };
    esp_err_t err = tinyusb_driver_install(&tusb_conf);
    LOOP(i, 1, err ? 0 : LEN(desc_str)) {
        ESP_LOGI(TAG, "Desc[%d] %s", i, desc_str[i]);
    }
    inited = !err;
    return err;
}

static esp_err_t usbd_common_exit() {
    if (!inited) return ESP_OK;
    inited = false;
#ifdef TARGET_IDF_5
    return tinyusb_driver_uninstall();
#else
    return ESP_ERR_NOT_SUPPORTED; // tusb_teardown not implemented yet
#endif
}

void tud_mount_cb(void) {
    if (mounted) return;
    ESP_LOGI(TAG, "mounted");
    mounted = true;
}

void tud_umount_cb(void) {
    if (!mounted) return;
    ESP_LOGI(TAG, "unmounted");
    mounted = false;
}

void tud_resume_cb(void) {
    ESP_LOGI(TAG, "resumed");
}

void tud_suspend_cb(bool en) {
    ESP_LOGI(TAG, "suspended (remote wakeup %s)", en ? "enabled" : "disabled");
}

/******************************************************************************
 * USBMode: CDC Device
 */

// see esp-idf-4.4/examples/peripherals/usb/tusb_console
// see esp-idf-4.4/examples/peripherals/usb/tusb_serial_device

#ifdef CONFIG_USB_CDC_DEVICE

#   ifdef CONFIG_USB_CDC_DEVICE_SERIAL
static void cdc_device_cb(int itf, cdcacm_event_t *event) {
    static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
    if (event->type == CDC_EVENT_RX) {
        size_t size = 0;
        esp_err_t err = tinyusb_cdcacm_read(itf, buf, sizeof(buf) - 1, &size);
        if (err) {
            ESP_LOGE(TAG, "CDC read error %s", esp_err_to_name(err));
        } else {
            ESP_LOGD(TAG, "CDC got data[%u]", size);
            ESP_LOG_BUFFER_HEXDUMP(TAG, buf, size, ESP_LOG_DEBUG);
            tinyusb_cdcacm_write_queue(itf, buf, size); // echo
            tinyusb_cdcacm_write_flush(itf, 0);
        }
    } else if (event->type == CDC_EVENT_RX_WANTED_CHAR) {
        ESP_LOGI(TAG, "CDC wanted char %c",
                event->rx_wanted_char_data.wanted_char);
    } else if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
        ESP_LOGI(TAG, "CDC line state DTR: %d, RTS: %d",
                event->line_state_changed_data.dtr,
                event->line_state_changed_data.rts);
    } else if (event->type == CDC_EVENT_LINE_CODING_CHANGED) {
        const cdc_line_coding_t *ptr = \
            event->line_coding_changed_data.p_line_coding;
        ESP_LOGI(TAG, "CDC line coding: %u,%u%c%c",
                ptr->bit_rate, ptr->data_bits,
                "NOEMS"[ptr->parity], "1H2"[ptr->stop_bits]);
    }
}
#   endif

esp_err_t cdc_device_init(usbmode_t prev) {
    if (cdc_enabled) return ESP_OK;
    esp_err_t err = usbd_common_init();
    tinyusb_config_cdcacm_t acm_conf = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
#   ifdef CONFIG_USB_CDC_DEVICE_SERIAL
#       ifdef TARGET_IDF_4
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
#       endif
        .callback_rx = cdc_device_cb,
        .callback_rx_wanted_char = cdc_device_cb,
        .callback_line_state_changed = cdc_device_cb,
        .callback_line_coding_changed = cdc_device_cb,
#   endif
    };
    if (!err) err = tusb_cdc_acm_init(&acm_conf);
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
    if (!err) err = esp_tusb_init_console(TINYUSB_CDC_ACM_0);
#   endif
    if (!err && ISDEV(prev)) usbmoded_reconnect();
    cdc_enabled = !err;
    return err;
}

esp_err_t cdc_device_exit(usbmode_t next) {
    esp_err_t err = ESP_OK;
    if (!cdc_enabled) return err;
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
    if (!err) err = esp_tusb_deinit_console(TINYUSB_CDC_ACM_0);
#   endif
#   ifdef TARGET_IDF_5
    if (!err) err = tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0);
#   endif
    if (!err && !ISDEV(next)) err = usbd_common_exit();
    cdc_enabled = false;
    return err;
}

#else // CONFIG_USB_CDC_DEVICE

esp_err_t cdc_device_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cdc_device_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USB_CDC_DEVICE

/******************************************************************************
 * USBMode: MSC Device
 */

// see esp-idf-5.2/examples/peripherals/usb/device/tusb_msc
// see esp-iot-solution/examples/usb/device/usb_msc_wireless_disk

#ifdef CONFIG_USB_MSC_DEVICE

#   define CHECK_LUN(lun, retval)                                           \
    {                                                                       \
        if ((lun) >= NUM_DISK) {                                            \
            ESP_LOGE(TAG, "%s invalid lun number %u", __func__, (lun));     \
            return retval;                                                  \
        }                                                                   \
        if (info[(lun)].pdrv == FF_DRV_NOT_USED) {                          \
            ESP_LOGE(TAG, "%s invalid lun drive %u", __func__, (lun));      \
            return retval;                                                  \
        }                                                                   \
    }

#   ifdef TARGET_IDF_4

void tud_msc_inquiry_cb(
    uint8_t lun, uint8_t vid[8], uint8_t pid[16], uint8_t rev[4]
) {
    CHECK_LUN(lun,);
    snprintf((char *)vid, 8, CONFIG_TINYUSB_DESC_MANUFACTURER_STRING);
    snprintf((char *)pid, 16, CONFIG_TINYUSB_DESC_MSC_STRING);
    snprintf((char *)rev, 4, Config.info.VER);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (lun < NUM_DISK && info[lun].pdrv == FF_DRV_NOT_USED)
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    CHECK_LUN(lun, false);
    filesys_acquire(info[lun].type, 100);
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *blkcnt, uint16_t *blksize) {
    CHECK_LUN(lun,);
    *blkcnt = info[lun].blkcnt;
    *blksize = info[lun].blksize;
    ESP_LOGD(TAG, "%s lun %u sector count %u, sector size %u",
             __func__, lun, *blkcnt, *blksize);
}

bool tud_msc_is_writable_cb(uint8_t lun) { CHECK_LUN(lun, 0); return 1; }

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t pc, bool start, bool le) {
    // Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
    // Start = 1 : active mode, if load_eject = 1 : load disk storage
    CHECK_LUN(lun, false);
    if (le) {
        if (start) {
            filesys_acquire(info[lun].type, 1);
        } else {
            filesys_release(info[lun].type);
        }
    }
    return true; NOTUSED(pc);
}

int32_t tud_msc_read10_cb(
    uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size
) {
    CHECK_LUN(lun, -1);
    esp_err_t err;
    size_t ssize = info[lun].blksize;
    size_t addr = lba * ssize + offset;
    size_t bcnt = size / (ssize ?: 1);
    if (!ssize || addr  % ssize || size % ssize) {
        ESP_LOGE(TAG, "MSC invalid lba(%u) offset(%u) size(%u) ssize(%u)",
                lba, offset, size, ssize);
        err = ESP_ERR_INVALID_ARG;
    } else if (info[lun].type == FILESYS_SDCARD) {
        err = sdmmc_read_sectors(info[lun].card, buffer, lba, bcnt);
    } else {
        err = wl_read(info[lun].wlhdl, addr, buffer, size);
    }
    return err ? -1 : size;
}

int32_t tud_msc_write10_cb(
    uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size
) {
    CHECK_LUN(lun, -1);
    esp_err_t err;
    size_t ssize = info[lun].blksize;
    size_t addr = lba * ssize + offset;
    size_t bcnt = size / (ssize ?: 1);
    if (!ssize || addr % ssize || size % ssize) {
        ESP_LOGE(TAG, "MSC invalid lba(%u) offset(%u) size(%u) ssize(%u)",
                lba, offset, size, ssize);
        err = ESP_ERR_INVALID_ARG;
    } else if (info[lun].type == FILESYS_SDCARD) {
        err = sdmmc_write_sectors(info[lun].card, buffer, lba, bcnt);
    } else if (( err = wl_erase_range(info[lun].wlhdl, addr, size) )) {
        ESP_LOGE(TAG, "MSC erase failed: %s", esp_err_to_name(err));
    } else {
        err = wl_write(info[lun].wlhdl, addr, buffer, size);
    }
    return err ? -1 : size;
}

int32_t tud_msc_scsi_cb(
    uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t size
) {
    CHECK_LUN(lun, 0);
    if (scsi_cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL) return 0;
    ESP_LOGW(TAG, "%s lun %u invoked %d", __func__, lun, scsi_cmd[0]);
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

#   endif // TARGET_IDF_4

esp_err_t msc_device_init(usbmode_t prev) {
    esp_err_t err = ESP_OK;
    if (msc_enabled) return err;
    if (
        !filesys_get_info(FILESYS_SDCARD, info + 0) && (
            !filesys_get_info(FILESYS_FLASH, info + NUM_DISK - 1)
            || info[NUM_DISK - 1].pdrv == FF_DRV_NOT_USED
        )
    ) {
        err = ESP_ERR_INVALID_STATE; // no initialized FAT filesystems
    }
    if (!err) err = usbd_common_init();
#   ifdef TARGET_IDF_5
    LOOPN(i, err ? 0 : NUM_DISK) {
        if (info[i].type == FILESYS_SDCARD) {
            const tinyusb_msc_sdmmc_config_t conf = {
                .card = info[i].card
            };
            err = tinyusb_msc_storage_init_sdmmc(&conf);
        } else {
            const tinyusb_msc_spiflash_config_t conf = {
                .wl_handle = info[i].wlhdl
            };
            err = tinyusb_msc_storage_init_spiflash(&conf);
        }
    }
    if (!err) err = tinyusb_msc_storage_mount("/usb");
#   endif
    if (!err && ISDEV(prev)) usbmoded_reconnect();
    msc_enabled = !err;
    return err;
}

esp_err_t msc_device_exit(usbmode_t next) {
    esp_err_t err = ESP_OK;
    if (!msc_enabled) return err;
#   ifdef TARGET_IDF_5
    err = tinyusb_msc_storage_deinit();
#   endif
    if (!err && !ISDEV(next)) err = usbd_common_exit();
    msc_enabled = false;
    return err;
}

#else // CONFIG_USB_MSC_DEVICE

esp_err_t msc_device_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t msc_device_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USB_MSC_DEVICE

/******************************************************************************
 * USBMode: HID Device
 */

// see esp-idf-5.2/examples/peripherals/usb/device/tusb_hid
// see esp-iot-solution/examples/usb/device/usb_hid_device

// implemented in usbmodeh.c
uint8_t str2keycode(const char *, uint8_t *);
const char * hid_keycode_str(uint8_t, uint8_t[6]);
// implemented in usbmoded.c
static const char * hid_btncode_str(uint8_t buttons);

#ifdef CONFIG_USB_HID_DEVICE

static const char * HID = "HID Device";

typedef struct {
    uint32_t report_id;
    union {
        uint8_t dial[2];
        hid_mouse_report_t mouse;
        hid_keyboard_report_t keyboard;
    };
} tinyusb_hid_report_t;

static struct {
    TaskHandle_t task;
    QueueHandle_t queue;
    SemaphoreHandle_t semphr;
} hid = { NULL, NULL, NULL };

static bool send_report(tinyusb_hid_report_t *rpt, bool intask, uint16_t ms) {
    if (!hid_enabled || !mounted) return false;
#ifdef CONFIG_USB_HID_DEVICE_TASK
    if (!intask) return hid.queue && xQueueSend(hid.queue, rpt, TIMEOUT(ms));
#endif
    bool sent = false;
    uint32_t rid = rpt->report_id;
    if (tud_suspended()) {
        ESP_LOGI(TAG, "%s suspended (reset queue)", HID);
        tud_remote_wakeup();
    } else if (rid == REPORT_ID_DIAL) {
        sent = tud_hid_report(rid, rpt->dial, 2);
        ESP_LOGI(HID, "dial Key 0x%04X SENT %d", *(uint16_t *)rpt->dial, sent);
    } else if (rid == REPORT_ID_MOUSE) {
        sent = tud_hid_report(rid, &rpt->mouse, sizeof(rpt->mouse));
        ESP_LOGI(HID, "mouse Btn %s X %d Y %d V %d H %d SENT %d",
                hid_btncode_str(rpt->mouse.buttons),
                rpt->mouse.x, rpt->mouse.y,
                rpt->mouse.wheel, rpt->mouse.pan, sent);
    } else if (rid == REPORT_ID_KEYBOARD) {
        sent = tud_hid_report(rid, &rpt->keyboard, sizeof(rpt->keyboard));
        uint8_t mod = rpt->keyboard.modifier;
        ESP_LOGI(HID, "keyboard Mod 0x%02X Key %s SENT %d",
                mod, hid_keycode_str(mod, rpt->keyboard.keycode), sent);
    }
#ifdef TARGET_IDF_4
#   ifdef CONFIG_USB_HID_DEVICE_TASK
    if (sent && !( sent = ulTaskNotifyTake(pdTRUE, TIMEOUT(ms)) == pdTRUE ))
#   else
    if (sent && !( sent = xSemaphoreTake(hid.semphr, TIMEOUT(ms)) == pdTRUE ))
#   endif
    {
        ESP_LOGW(HID, "report not sent");
    }
#endif
    return sent;
}

void tud_hid_report_complete_cb(uint8_t i, uint8_t const *r, uint8_t l) {
    if (hid.task) xTaskNotifyGive(hid.task);
    if (hid.semphr) xSemaphoreGive(hid.semphr);
    return; NOTUSED(i); NOTUSED(r); NOTUSED(l);
}

uint16_t tud_hid_get_report_cb(
    uint8_t i, uint8_t r, hid_report_type_t t, uint8_t *b, uint16_t l
) {
    return 0; NOTUSED(i); NOTUSED(r); NOTUSED(t); NOTUSED(b); NOTUSED(l);
}

void tud_hid_set_report_cb(
    uint8_t i, uint8_t r, hid_report_type_t t, const uint8_t *b, uint16_t l
) {
    return; NOTUSED(i); NOTUSED(r); NOTUSED(t); NOTUSED(b); NOTUSED(l);
}

#if defined(CONFIG_USB_HID_DEVICE_TASK) && defined(TARGET_IDF_4)
static void hid_device_task(void *arg) {
    tinyusb_hid_report_t report;
    while (1) {
        if (xQueueReceive(hid.queue, &report, TIMEOUT(100)))
            send_report(&report, true, 100);
    }
}
#endif

esp_err_t hid_device_init(usbmode_t prev) {
    if (hid_enabled) return ESP_OK;
    esp_err_t err = usbd_common_init();
#ifdef TARGET_IDF_4
    if (!err && (
#   ifdef CONFIG_USB_HID_DEVICE_TASK
        !( hid.queue = xQueueCreate(10, sizeof(tinyusb_hid_report_t)) ) ||
        !xTaskCreate(hid_device_task, "USB-HID", 4096, NULL, 5, &hid.task) ||
        !xTaskNotifyGive(hid.task)
#   else
        !( hid.semphr = xSemaphoreCreateBinary() )
#   endif
    )) {
        err = ESP_ERR_NO_MEM;
        hid_device_exit();
    }
#endif
    if (!err && ISDEV(prev)) usbmoded_reconnect();
    hid_enabled = !err;
    return err;
}

esp_err_t hid_device_exit(usbmode_t next) {
    if (!hid_enabled) return ESP_OK;
#ifdef TARGET_IDF_4
    TRYNULL(hid.task, vTaskDelete); // avoid memory leak and enable reentry
    TRYNULL(hid.queue, vQueueDelete);
    TRYNULL(hid.semphr, vSemaphoreDelete);
#endif
    hid_enabled = false;
    return ISDEV(next) ? ESP_OK : usbd_common_exit();
}

#else // CONFIG_USB_HID_DEVICE

esp_err_t hid_device_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t hid_device_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_USB_HID_DEVICE

/******************************************************************************
 * USBMode: HID Device Utilities
 */

static const char * BUTTON_STR[] = {
    "Left", "Right", "Middle", "Backward", "Forward"
};

static uint8_t str2btncode(const char *str) {
    LOOPN(i, LEN(BUTTON_STR)) {
        if (!strcasecmp(str, BUTTON_STR[i])) return 1 << i;
    }
    return 0;
}

static const char * hid_btncode_str(uint8_t buttons) {
    static char buf[64];
    size_t blen = sizeof(buf), size = 0;
    LOOPN(i, LEN(BUTTON_STR)) {
        if (buttons & (1 << i)) {
            size += snprintf(buf + size, blen - size, "%s%s",
                i ? " | " : "", BUTTON_STR[i]);
        } else {
            buf[size] = '\0';
        }
    }
    return buf;
}

#ifndef CONFIG_USB_HID_HOST
uint8_t str2keycode(const char *str, uint8_t *mod) {
    static uint8_t a2k[128][2] = { HID_ASCII_TO_KEYCODE };
    if (!str || !strlen(str)) return 0;
    if (mod) *mod = a2k[str[0]][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
    return a2k[str[0]][1];
}

const char * hid_keycode_str(uint8_t mod, uint8_t keycode[6]) {
    static char buf[6 * 2 + 1] = { 0 };
    static uint8_t k2a[128][2] = { HID_KEYCODE_TO_ASCII }, val;
    bool shift = mod & (
        KEYBOARD_MODIFIER_LEFTSHIFT |
        KEYBOARD_MODIFIER_RIGHTSHIFT);
    LOOPN(i, 6) {
        switch (val = k2a[keycode[i]][shift]) {
            case '\0': strcat(buf, "\\0"); break;
            case '\a': strcat(buf, "\\a"); break;
            case '\b': strcat(buf, "\\b"); break;
            case '\n': strcat(buf, "\\n"); break;
            case '\r': strcat(buf, "\\r"); break;
            case '\t': strcat(buf, "\\t"); break;
            case '\v': strcat(buf, "\\v"); break;
            default:
                if (0x20 <= val && val <= 0x7E) { // ASCII printables
                    strncat(buf, &val, 1);
                } else {
                    size_t idx = strlen(buf);
                    hexdumps(buf + idx, &val, 1, sizeof(buf) - idx);
                }
        }
    }
    return buf;
}
#endif // !CONFIG_USB_HID_HOST

bool hid_report_dial(hid_dial_keycode_t k) {
#ifdef CONFIG_USB_HID_DEVICE
    tinyusb_hid_report_t report = {
        .report_id = REPORT_ID_DIAL,
        .dial = { k, (k == DIAL_L || k == DIAL_LF) ? 0xFF : 0 }
    };
    return send_report(&report, false, 100);
#else
    return false; NOTUSED(k);
#endif
}

bool hid_report_dial_button(uint32_t ms) {
    bool sent = hid_report_dial(DIAL_DN);
    if (sent && ms) {
        msleep(ms);
        sent = hid_report_dial(DIAL_UP);
    }
    return sent;
}

bool hid_report_mouse(uint8_t b, int8_t x, int8_t y, int8_t v, int8_t h) {
#ifdef CONFIG_USB_HID_DEVICE
    tinyusb_hid_report_t report = {
        .report_id = REPORT_ID_MOUSE,
        .mouse = { b, x, y, v, h },
    };
    return send_report(&report, false, 100);
#else
    return false; NOTUSED(b); NOTUSED(x); NOTUSED(y); NOTUSED(v); NOTUSED(h);
#endif
}

bool hid_report_mouse_click(const char *str, uint32_t ms) {
    uint8_t btncode = str2btncode(str);
    bool sent = hid_report_mouse_button(btncode);
    if (sent && btncode && ms) {
        msleep(ms);
        sent = hid_report_mouse_button(0);
    }
    return sent;
}

bool hid_report_keyboard(uint8_t mod, const uint8_t *src, size_t len) {
#ifdef CONFIG_USB_HID_DEVICE
    tinyusb_hid_report_t report = {
        .report_id = REPORT_ID_KEYBOARD,
        .keyboard = { .modifier = mod, .keycode = { 0 } },
    };
    memcpy(report.keyboard.keycode, src, MIN(len, 6));
    return send_report(&report, false, 100);
#else
    return false; NOTUSED(mod); NOTUSED(src); NOTUSED(len);
#endif
}

bool hid_report_keyboard_press(const char *str, uint32_t ms) {
    uint8_t modifier = 0, keycode = str2keycode(str, &modifier);
    bool sent = hid_report_keyboard(modifier, &keycode, !!keycode);
    if (sent && keycode && ms) {
        msleep(ms);
        sent = hid_report_keyboard(0, NULL, 0);
    }
    return sent;
}

#endif // CONFIG_USE_USB
