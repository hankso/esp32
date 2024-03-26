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
#include "../include_private/usb_descriptors.h"
#include "../include_private/descriptors_control.h"

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

static bool mounted = false, inited = false;
static filesys_info_t info[NUM_DISK];

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

static esp_err_t usbd_common_init() {
    // see esp-idf-4.4/components/tinyusb/additions/src/usb_descriptors.c
    // see esp_tinyusb/usb_descriptors.c
#ifdef TARGET_IDF_5
#   define desc_dev descriptor_dev_default
#   define desc_str descriptor_str_default
#else
#   define desc_dev descriptor_kconfig
#   define desc_str descriptor_str_kconfig
#endif
    if (inited) return ESP_OK;
    LOOPN(i, NUM_DISK) { info[i].pdrv = FF_DRV_NOT_USED; }
    int ver[3];
    if (parse_all(Config.info.VER, ver, 3) >= 2)
        desc_dev.bcdDevice = (uint8_t)(ver[0] << 8) | (uint8_t)ver[1];
    if (strlen(Config.info.UID)) desc_str[3] = Config.info.UID;
    tinyusb_config_t tusb_conf = {
        .external_phy = false,
        .string_descriptor = desc_str,
#ifdef TARGET_IDF_5
        .device_descriptor = &desc_dev,
#else
        .descriptor = &desc_dev,
#endif
    };
    esp_err_t err = tinyusb_driver_install(&tusb_conf);
    LOOP(i, 1, err ? 0 : LEN(desc_str)) {
        ESP_LOGI(TAG, "Desc[%d] %s", i, desc_str[i]);
    }
    if (!err) inited = true;
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

// TODO: mute error `tusb_cdc: Interface is not initialized.`

#ifdef CONFIG_USB_CDC_DEVICE_SERIAL
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
#endif // CONFIG_USB_CDC_DEVICE_SERIAL

#ifdef CONFIG_USB_CDC_DEVICE
esp_err_t cdc_device_init() {
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
    return err;
}

esp_err_t cdc_device_exit() {
    esp_err_t err = ESP_OK;
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
    if (!err) err = esp_tusb_deinit_console(TINYUSB_CDC_ACM_0);
#   endif
#   ifdef TARGET_IDF_5
    if (!err) err = tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0);
#   endif
    if (!err) err = usbd_common_exit();
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

#if defined(CONFIG_USB_MSC_DEVICE) && defined(TARGET_IDF_4)

static bool msc_enabled = false;

#   define CHECK_LUN(lun, ret)                                              \
    {                                                                       \
        if (!msc_enabled) return ret;                                       \
        if ((lun) >= NUM_DISK) {                                            \
            ESP_LOGE(TAG, "%s invalid lun number %u", __func__, (lun));     \
            return ret;                                                     \
        }                                                                   \
        if (info[(lun)].pdrv == FF_DRV_NOT_USED) {                          \
            ESP_LOGE(TAG, "%s invalid lun drive %u", __func__, (lun));      \
            return ret;                                                     \
        }                                                                   \
    }

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
#endif // CONFIG_USB_MSC_DEVICE && TARGET_IDF_4

#ifdef CONFIG_USB_MSC_DEVICE

static esp_err_t probe_disks() {
    esp_err_t err = ESP_OK;
    if (
        !filesys_get_info(FILESYS_SDCARD, info + 0) && (
            !filesys_get_info(FILESYS_FLASH, info + NUM_DISK - 1)
            || info[NUM_DISK - 1].pdrv == FF_DRV_NOT_USED
        )
    ) {
        err = ESP_ERR_INVALID_STATE; // no initialized FAT filesystems
    }
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
    return err;
}

esp_err_t msc_device_init() {
    esp_err_t err = usbd_common_init();
    if (!err) err = probe_disks();
    msc_enabled = !err;
    return err;
}

esp_err_t msc_device_exit() {
#   ifdef TARGET_IDF_5
    tinyusb_msc_storage_deinit();
#   endif
    msc_enabled = false;
    return usbd_common_exit();
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

#ifdef CONFIG_USB_HID_HOST
// implemented in usbmodeh.c
uint8_t str2keycode(const char *, uint8_t *);
const char * hid_keycode_str(uint8_t, uint8_t[6]);
#else
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
#endif

#ifdef CONFIG_USB_HID_DEVICE

#   ifdef TARGET_IDF_4

static bool hid_enabled = false;

typedef struct {
    uint32_t report_id;
    union {
        hid_mouse_report_t mouse;
        hid_keyboard_report_t keyboard;
        hid_gamepad_report_t gamepad;
    };
} tinyusb_hid_report_t;

static struct {
    TaskHandle_t task;
    QueueHandle_t queue;
    SemaphoreHandle_t semphr;
} hid = { NULL, NULL, NULL };

static void hid_device_task(void *arg) {
    tinyusb_hid_report_t report;
    while (1) {
        if (!xQueueReceive(hid.queue, &report, TIMEOUT(100))) continue;
        if (tud_suspended()) {
            ESP_LOGI(TAG, "HID suspended (reset queue)");
            tud_remote_wakeup();
            xQueueReset(hid.queue);
            continue;
        }
        if (report.report_id == REPORT_ID_MOUSE) {
            bool ret = tud_hid_report(
                report.report_id, &report.mouse, sizeof(report.mouse));
            ESP_LOGD(TAG, "HID mouse Btn 0x%02X X %d Y %d V %d H %d R %d",
                    report.mouse.buttons,
                    report.mouse.x, report.mouse.y,
                    report.mouse.wheel, report.mouse.pan, ret);
        } else if (report.report_id == REPORT_ID_KEYBOARD) {
            bool ret = tud_hid_report(
                report.report_id, &report.keyboard, sizeof(report.keyboard));
            uint8_t mod = report.keyboard.modifier;
            ESP_LOGD(TAG, "HID keyboard Mod 0x%02X Key %s R %d",
                    mod, hid_keycode_str(mod, report.keyboard.keycode), ret);
        }
        if (!ulTaskNotifyTake(pdTRUE, TIMEOUT(100)))
            ESP_LOGW(TAG, "HID report not sent");
    }
}

void tud_hid_report_complete_cb(uint8_t i, uint8_t const *r, uint8_t l) {
    if (!hid_enabled) return;
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

bool hid_report_mouse(uint8_t b, int8_t x, int8_t y, int8_t v, int8_t h) {
    if (!hid_enabled || !mounted || !hid.queue) return false;
    tinyusb_hid_report_t report = {
        .report_id = REPORT_ID_MOUSE,
        .mouse = { b, x, y, v, h },
    };
    return xQueueSend(hid.queue, &report, TIMEOUT(200));
}

bool hid_report_keyboard(uint8_t mod, const uint8_t *src, size_t len) {
    if (!hid_enabled || !mounted || !hid.queue) return false;
    tinyusb_hid_report_t report = {
        .report_id = REPORT_ID_KEYBOARD,
        .keyboard = { .modifier = mod, .keycode = { 0 } },
    };
    memcpy(report.keyboard.keycode, src, MIN(len, 6));
    return xQueueSend(hid.queue, &report, TIMEOUT(200));
}

#   else // TARGET_IDF_4

bool hid_report_mouse(uint8_t b, int8_t x, int8_t y, int8_t v, int8_t h) {
    if (!hid_enabled || !mounted) return false;
    bool ret = tud_hid_report_mouse(HID_ITF_PROTOCOL_MOUSE, b, x, y, v, h);
    ESP_LOGI(TAG, "HID mouse Btn 0x%02X X %d Y %d V %d H %d R %d",
            b, x, y, v, h, ret);
    return ret;
}

bool hid_report_keyboard(uint8_t mod, const uint8_t *src, size_t len) {
    if (!hid_enabled || !mounted) return false;
    bool ret;
    uint8_t keycode[6] = { 0 };
    if (len) {
        LOOPN(i, sizeof(keycode)) { keycode[i] = i < len ? src[i] : 0; }
        ret = tud_hid_report_keyboard(HID_ITF_PROTOCOL_KEYBOARD, mod, keycode);
    } else {
        ret = tud_hid_report_keyboard(HID_ITF_PROTOCOL_KEYBOARD, mod, NULL);
    }
    ESP_LOGI(TAG, "HID keyboard Mod 0x%02X Key %s R %d",
            mod, len ? hid_keycode_str(mod, keycode) : "", ret);
    return ret;
}

#   endif // TARGET_IDF_4

bool hid_report_keypress(const char *str, uint32_t ms) {
    uint8_t modifier = 0, keycode = str2keycode(str, &modifier);
    bool ret = hid_report_keyboard(modifier, &keycode, !!keycode);
    if (ret && keycode && ms) {
        msleep(ms);
        ret = hid_report_keyboard(0, NULL, 0);
    }
    return ret;
}

esp_err_t hid_device_init() {
    esp_err_t err = usbd_common_init();
#   ifdef TARGET_IDF_4
    if (!err && (
        // !( hid.semphr = xSemaphoreCreateBinary() ) ||
        !( hid.queue = xQueueCreate(10, sizeof(tinyusb_hid_report_t)) ) ||
        !xTaskCreate(hid_device_task, "hid", 4096, NULL, 5, &hid.task) ||
        !xTaskNotifyGive(hid.task)
    )) {
        err = ESP_ERR_NO_MEM;
        hid_device_exit();
    }
#   endif
    hid_enabled = !err;
    return err;
}

esp_err_t hid_device_exit() {
#   ifdef TARGET_IDF_4
    TRYNULL(hid.task, vTaskDelete); // avoid memory leak and enable reentry
    TRYNULL(hid.queue, vQueueDelete);
    TRYNULL(hid.semphr, vSemaphoreDelete);
#   endif
    hid_enabled = false;
    return usbd_common_exit();
}

#else // CONFIG_USB_HID_DEVICE

esp_err_t hid_device_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t hid_device_exit() { return ESP_ERR_NOT_SUPPORTED; }

bool hid_report_mouse(uint8_t b, int8_t x, int8_t y, int8_t v, int8_t h) {
    return false; NOTUSED(b); NOTUSED(x); NOTUSED(y); NOTUSED(v); NOTUSED(h);
}

bool hid_report_keyboard(uint8_t m, uint8_t *k, size_t l) {
    return false; NOTUSED(m); NOTUSED(k); NOTUSED(l);
}

bool hid_report_keypress(const char *s, uint32_t m) {
    return false; NOTUSED(s); NOTUSED(m);
}

#endif // CONFIG_USB_HID_DEVICE

#endif // CONFIG_USE_USB
