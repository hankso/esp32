/*
 * File: usbdev.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024-03-25 18:26:20
 */

#include "usbmode.h"
#include "filesys.h"
#include "config.h"

#ifdef CONFIG_BASE_USE_USB

#include "tinyusb.h"
#include "usb/usb_host.h" // for usb_device_desc_t

#ifdef CONFIG_BASE_USB_CDC_DEVICE
#   include "tusb_cdc_acm.h"
#endif

#ifdef CONFIG_BASE_USB_MSC_DEVICE
#   ifdef IDF_TARGET_V4
#       include "sdmmc_cmd.h"
#   else
#       include "tusb_msc_storage.h"
#   endif
#endif

#ifdef CONFIG_BASE_USB_HID_DEVICE
#   ifdef IDF_TARGET_V4
#       include "freertos/FreeRTOS.h"
#       include "freertos/task.h"
#       include "freertos/queue.h"
#       include "freertos/semphr.h"
#   else
#       include "class/hid/hid_device.h"
#   endif
#endif

#define NUM_DISK 1 // currently only one endpoint is supported

static const char *TAG = "USBDevice";

static filesys_info_t info[NUM_DISK];
static bool mounted = false, inited = false;
static bool cdc_enabled = false, msc_enabled = false, hid_enabled = false;

/*
 * Helper functions
 */

void usbdev_status(usbmode_t mode) {
    printf("inited: %s, mounted: %s\n",
            inited ? "true" : "false", mounted ? "true" : "false");
#ifdef CONFIG_BASE_USB_CDC_DEVICE
    if (mode == CDC_DEVICE) {
#   ifdef CONFIG_BASE_USB_CDC_DEVICE_SERIAL
        puts("Running as CDC serial device");
#   endif
#   ifdef CONFIG_BASE_USB_CDC_DEVICE_CONSOLE
        puts("Running as CDC console device");
#   endif
    }
#endif
#ifdef CONFIG_BASE_USB_MSC_DEVICE
    if (mode == MSC_DEVICE) {
        LOOPN(i, NUM_DISK) {
            if (info[i].pdrv == FF_DRV_NOT_USED) {
                printf("Disk[%d]: not mouned / supported\n", i);
                continue;
            }
            printf("Disk[%d]: pdrv=%u, ssize=%u, total=%s\n",
                i, info[i].pdrv, info[i].blksize, format_size(info[i].total));
        }
    }
#endif
#ifdef CONFIG_BASE_USB_HID_DEVICE
    if (mode == HID_DEVICE) puts("Running as HID keybd & mouse device");
#endif
}

bool usbdev_interest(const void *arg) {
    const usb_device_desc_t *desc = arg;
    return (
        desc->idVendor == HIDTool.vid ||
#ifdef CONFIG_BASE_USB_CDC_HOST
        desc->bDeviceClass == TUSB_CLASS_CDC ||
#endif
#ifdef CONFIG_BASE_USB_MSC_HOST
        desc->bDeviceClass == TUSB_CLASS_MSC ||
#endif
#ifdef CONFIG_BASE_USB_HID_HOST
        desc->bDeviceClass == TUSB_CLASS_HID ||
#endif
        (
            desc->bDeviceClass == TUSB_CLASS_MISC &&
            desc->bDeviceSubClass == MISC_SUBCLASS_COMMON &&
            desc->bDeviceProtocol == MISC_PROTOCOL_IAD
        )
    );
}

static bool usbdev_reconnect() {
    bool ret = tud_disconnect();
    if (ret) {
        msleep(100);
        ret = tud_connect();
    }
    return ret;
}

/*
 * USB Descriptor hacks
 */

// see idf-v4.4/tinyusb/additions/include_private/descriptors_control.h
// see idf-v4.4/tinyusb/additions/src/usb_descriptors.c
// see esp_tinyusb/usb_descriptors.c
// see idf-v4.4-tinyusb-hid.patch

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
#ifdef CONFIG_BASE_USB_HID_DEVICE
    size_t blen = CFG_TUD_HID_EP_BUFSIZE, rlen = HIDTool.dlen;
#else
    size_t blen = 0, rlen = 0;
#endif
    if (hid_enabled) {                  // stridx, proto,   EPI, size, poll
        uint8_t h[] = { TUD_HID_DESCRIPTOR(itf, 6, 0, rlen, 0x84, blen, 10 ) };
        memcpy(buf + total, h, sizeof(h));
        itf += 1; // for ITF_NUM_HID
        total += sizeof(h);
        // SUBCLASS=0, PROTO=0 for mixture of mouse & keyboard
    }
    uint8_t desc[] = {
        TUD_CONFIG_DESCRIPTOR(
            1, itf, 0, total, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500
        )
    };
    memcpy(buf, desc, sizeof(desc));
    return buf;
}

#ifdef IDF_TARGET_V4
uint8_t const *tud_descriptor_configuration_cb(uint8_t i) { // overwrite weak
    return config_desc(); NOTUSED(i);
}
#endif

static tusb_desc_device_t desc_dev = {
    .bLength = sizeof(desc_dev),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,

#if CFG_TUD_CDC
    // Use Interface Association Descriptor (IAD) for CDC
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
#endif

    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = 0x0000,
    .idProduct = 0x0000,
    .bcdDevice = CONFIG_TINYUSB_DESC_BCD_DEVICE,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01
};

static const char *desc_str[] = {
    (char[]){ 0x09, 0x04 },                     // 0: language English (0x0409)
    "",                                         // 1: Manufacturer
    "",                                         // 2: Product
    "",                                         // 3: Serial
    "CDC Device",                               // 4: CDC Interface
    "MSC Device",                               // 5: MSC Interface
    "HID Device",                               // 6: HID Interface
};

static esp_err_t usbd_common_init() {
    if (inited) return ESP_OK;
    LOOPN(i, NUM_DISK) { info[i].pdrv = FF_DRV_NOT_USED; }

    desc_dev.idVendor                   = HIDTool.vid;
    desc_dev.idProduct                  = HIDTool.pid;
    desc_dev.bcdDevice                  = HIDTool.ver;
    desc_str[desc_dev.iManufacturer]    = HIDTool.vendor;
    desc_str[desc_dev.idProduct]        = Config.info.NAME;
    desc_str[desc_dev.iSerialNumber]    = HIDTool.serial;

    tinyusb_config_t tusb_conf = {
        .external_phy = false,
#ifdef IDF_TARGET_V4
        .descriptor = &desc_dev,
        .string_descriptor = desc_str,
#else
        .device_descriptor = &desc_dev,
        .string_descriptor = desc_str,
        .string_descriptor_count = LEN(desc_str),
        .configuration_descriptor = config_desc(),
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
#ifdef IDF_TARGET_V4
    return ESP_ERR_NOT_SUPPORTED; // tusb_teardown not supported yet
#else
    return tinyusb_driver_uninstall();
#endif
}

#ifdef IDF_TARGET_V4
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
#endif

void tud_resume_cb(void) {
    ESP_LOGI(TAG, "resumed");
}

void tud_suspend_cb(bool en) {
    ESP_LOGI(TAG, "suspended (remote wakeup %s)", en ? "enabled" : "disabled");
}

/*
 * USBMode: CDC Device
 */

// see esp-idf-4.4/examples/peripherals/usb/tusb_console
// see esp-idf-4.4/examples/peripherals/usb/tusb_serial_device

#ifdef CONFIG_BASE_USB_CDC_DEVICE

#   ifdef CONFIG_BASE_USB_CDC_DEVICE_SERIAL
static void cdc_device_cb(int itf, cdcacm_event_t *event) {
    if (event->type == CDC_EVENT_RX) {
        size_t size = 0;
        uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
        esp_err_t err = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &size);
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
        ESP_LOGI(TAG, "CDC line coding: %" PRIu32 ",%u%c%c",
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
#   ifdef CONFIG_BASE_USB_CDC_DEVICE_SERIAL
#       ifdef IDF_TARGET_V4
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
#       endif
        .callback_rx = cdc_device_cb,
        .callback_rx_wanted_char = cdc_device_cb,
        .callback_line_state_changed = cdc_device_cb,
        .callback_line_coding_changed = cdc_device_cb,
#   endif
    };
    if (!err) err = tusb_cdc_acm_init(&acm_conf);
#   ifdef CONFIG_BASE_USB_CDC_DEVICE_CONSOLE
    if (!err) err = esp_tusb_init_console(TINYUSB_CDC_ACM_0);
#   endif
    if (!err && ISDEV(prev)) usbdev_reconnect();
    cdc_enabled = !err;
    return err;
}

esp_err_t cdc_device_exit(usbmode_t next) {
    esp_err_t err = ESP_OK;
    if (!cdc_enabled) return err;
#   ifdef CONFIG_BASE_USB_CDC_DEVICE_CONSOLE
    if (!err) err = esp_tusb_deinit_console(TINYUSB_CDC_ACM_0);
#   endif
#   ifndef IDF_TARGET_V4
    if (!err) err = tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0);
#   endif
    if (!err && !ISDEV(next)) err = usbd_common_exit();
    cdc_enabled = false;
    return err;
}

#else // CONFIG_BASE_USB_CDC_DEVICE

esp_err_t cdc_device_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cdc_device_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_USB_CDC_DEVICE

/*
 * USBMode: MSC Device
 */

// see esp-idf-5.2/examples/peripherals/usb/device/tusb_msc
// see esp-iot-solution/examples/usb/device/usb_msc_wireless_disk

#ifdef CONFIG_BASE_USB_MSC_DEVICE
#   if !defined(CONFIG_BASE_FFS_FAT) && !defined(CONFIG_BASE_USE_SDFS)
#       warning "Internal Flash and SDCard storage are not supported"
#   endif

#   define CHECK_LUN(lun, retval)                                           \
    do {                                                                    \
        if ((lun) >= NUM_DISK) {                                            \
            ESP_LOGE(TAG, "%s invalid lun number %u", __func__, (lun));     \
            return retval;                                                  \
        }                                                                   \
        if (info[(lun)].pdrv == FF_DRV_NOT_USED) {                          \
            ESP_LOGE(TAG, "%s invalid lun drive %u", __func__, (lun));      \
            return retval;                                                  \
        }                                                                   \
    } while (0)

#   ifdef IDF_TARGET_V4

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

#   endif // IDF_TARGET_V4

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
#   ifndef IDF_TARGET_V4
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
    if (!err && ISDEV(prev)) usbdev_reconnect();
    msc_enabled = !err;
    return err;
}

esp_err_t msc_device_exit(usbmode_t next) {
    esp_err_t err = ESP_OK;
    if (!msc_enabled) return err;
#   ifndef IDF_TARGET_V4
    tinyusb_msc_storage_deinit();
#   endif
    if (!err && !ISDEV(next)) err = usbd_common_exit();
    msc_enabled = false;
    return err;
}

#else // CONFIG_BASE_USB_MSC_DEVICE

esp_err_t msc_device_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t msc_device_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_USB_MSC_DEVICE

/*
 * USBMode: HID Device
 */

// see esp-idf-5.2/examples/peripherals/usb/device/tusb_hid
// see esp-iot-solution/examples/usb/device/usb_hid_device

#ifdef CONFIG_BASE_USB_HID_DEVICE

static const char * HID = "HID Device";

static struct {
    TaskHandle_t task;
    QueueHandle_t queue;
    SemaphoreHandle_t semphr;
} hid = { NULL, NULL, NULL };

static bool send_report(const hid_report_t *rpt, bool intask, TickType_t tout) {
    if (!hid_enabled || !HID_VALID_REPORT(rpt)) return false;
#ifdef CONFIG_BASE_USB_HID_DEVICE_TASK
    if (!intask) return hid.queue && xQueueSend(hid.queue, rpt, tout);
#endif
    if (tud_suspended()) {
        ESP_LOGI(TAG, "%s suspended (reset queue)", HID);
        tud_remote_wakeup();
        return false;
    }
    bool sent = tud_hid_report(rpt->id, (void *)rpt, rpt->size);
#ifdef IDF_TARGET_V4
#   ifdef CONFIG_BASE_USB_HID_DEVICE_TASK
    if (sent && !( sent = ulTaskNotifyTake(pdTRUE, tout) == pdTRUE ))
#   else
    if (sent && !( sent = xSemaphoreTake(hid.semphr, tout) == pdTRUE ))
#   endif
        ESP_LOGW(HID, "report not sent");
#endif
    return sent;
}

bool hidu_send_report(const hid_report_t *report) {
    return send_report(report, false, TIMEOUT(100));
}

#ifdef IDF_TARGET_V4
void tud_hid_report_complete_cb(uint8_t i, uint8_t const *r, uint8_t l) {
#else
void tud_hid_report_complete_cb(uint8_t i, uint8_t const *r, uint16_t l) {
#endif
    if (hid.task) xTaskNotifyGive(hid.task);
    if (hid.semphr) xSemaphoreGive(hid.semphr);
    return; NOTUSED(i); NOTUSED(r); NOTUSED(l);
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t i) { // overwrite weak
    return HIDTool.desc; NOTUSED(i);
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

#if defined(CONFIG_BASE_USB_HID_DEVICE_TASK) && defined(IDF_TARGET_V4)
static void hid_device_task(void *arg) {
    hid_report_t report;
    TickType_t timeout = TIMEOUT(100);
    while (1) {
        if (xQueueReceive(hid.queue, &report, timeout))
            send_report(&report, true, timeout);
    }
}
#endif

esp_err_t hid_device_init(usbmode_t prev) {
    if (hid_enabled) return ESP_OK;
    esp_err_t err = usbd_common_init();
#ifdef IDF_TARGET_V4
    if (!err && (
#   ifdef CONFIG_BASE_USB_HID_DEVICE_TASK
        !( hid.queue = xQueueCreate(10, sizeof(hid_report_t)) ) ||
        !xTaskCreate(hid_device_task, "USB-HID", 4096, NULL, 5, &hid.task) ||
        !xTaskNotifyGive(hid.task)
#   else
        !( hid.semphr = xSemaphoreCreateBinary() )
#   endif
    )) {
        err = ESP_ERR_NO_MEM;
        TRYNULL(hid.task, vTaskDelete);
        TRYNULL(hid.queue, vQueueDelete);
        TRYNULL(hid.semphr, vSemaphoreDelete);
    }
#endif
    if (!err && ISDEV(prev)) usbdev_reconnect();
    hid_enabled = !err;
    return err;
}

esp_err_t hid_device_exit(usbmode_t next) {
    if (!hid_enabled) return ESP_OK;
#ifdef IDF_TARGET_V4
    TRYNULL(hid.task, vTaskDelete);
    TRYNULL(hid.queue, vQueueDelete);
    TRYNULL(hid.semphr, vSemaphoreDelete);
#endif
    hid_enabled = false;
    return ISDEV(next) ? ESP_OK : usbd_common_exit();
}

#else // CONFIG_BASE_USB_HID_DEVICE

esp_err_t hid_device_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t hid_device_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_USB_HID_DEVICE

#endif // CONFIG_BASE_USE_USB
