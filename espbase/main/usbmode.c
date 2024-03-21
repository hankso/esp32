/* 
 * File: usbmode.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#include "usbmode.h"
#include "filesys.h"
#include "config.h"

#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_USE_USB)

#include "usb/usb_host.h"

#include "tinyusb.h"
#include "../include_private/usb_descriptors.h"

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#   include "driver/usb_serial_jtag.h"
#endif

#ifdef CONFIG_USB_CDC_DEVICE
#   include "tusb_cdc_acm.h"
#endif

#ifdef CONFIG_USB_CDC_HOST
#   include "usb/cdc_acm_host.h"        // idf add-dependency usb_host_cdc_acm
#endif

#ifdef CONFIG_USB_MSC_DEVICE
#   ifdef TARGET_IDF_5
#       include "tusb_msc_storage.h"    // idf add-dependency esp_tinyusb
#   else
#       include "sdmmc_cmd.h"
#   endif
#endif

#ifdef CONFIG_USB_MSC_HOST
#   include "msc_host.h"                // idf add-dependency usb_host_msc
#   include "msc_host_vfs.h"
#   include "../private_include/msc_common.h"
#endif

#if !defined(CONFIG_FFS_FAT) && !defined(CONFIG_USE_SDFS)
#   warning "Internal Flash and SDCard storage are not supported"
#   undef CONFIG_USB_MSC_DEVICE
#endif

#define MSC_MP                  "/msc"
#define NUM_DISK                1 // currently only one endpoint is supported
#define TIMEOUT_IDLE            10
#define TIMEOUT_LOOP            50
#define TIMEOUT_WAIT            200
#define ESP_ERR_DISABLED        0x501
#define ESP_ERR_NOT_INITED      0x502
#define ESP_ERR_PENDING_REBOOT  0x503

static const char *TAG = "USBMode";

static struct {
    int state;
    bool running;
    void * vfs_hdl;
    union {
        uint8_t address;
        uint32_t vid_pid;
    };
    esp_err_t err;
    filesys_info_t info[NUM_DISK];
    SemaphoreHandle_t usblib_sem, client_sem;
} ctx;

/******************************************************************************
 * Helper functions
 */

static bool acquire(SemaphoreHandle_t sem, uint32_t msec) {
    return sem ? ACQUIRE(sem, msec) : false;
}

static bool release(SemaphoreHandle_t sem) {
    return sem ? RELEASE(sem) : false;
}

static uint32_t usb_dev_vid_pid(void *dev_hdl) {
    const usb_device_desc_t *desc;
    esp_err_t err = usb_host_get_device_descriptor(dev_hdl, &desc);
    if (err) return 0;
    return desc->idVendor << 16 | desc->idProduct;
}

static const char * vid_pid_str(uint32_t vp) {
    static char buf[14]; // 0xXXXX:0xXXXX
    snprintf(buf, sizeof(buf), "0x%04X:0x%04X", vp >> 16, vp & 0xFFFF);
    return buf;
}

#ifdef TARGET_IDF_5
#   define desc_dev descriptor_dev_default
#   define desc_str descriptor_str_default
#else
#   define desc_dev descriptor_kconfig
#   define desc_str descriptor_str_kconfig
#endif

static bool wanted_device(const usb_device_desc_t *desc) {
    return (
        desc->idVendor == desc_dev.idVendor || // Espressif VID
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

static void print_devinfo(usb_device_handle_t dev_hdl) {
    esp_err_t err;
    usb_device_info_t dev_info;
    const usb_device_desc_t *dev_desc;
    const usb_config_desc_t *cfg_desc;
    if (
        ( err = usb_host_device_info(dev_hdl, &dev_info) ) ||
        ( err = usb_host_get_device_descriptor(dev_hdl, &dev_desc) ) ||
        ( err = usb_host_get_active_config_descriptor(dev_hdl, &cfg_desc) )
    ) {
        ESP_LOGE(TAG, "Could not detect device: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "USB Client: Found new device: %d", dev_info.dev_addr);
    if (dev_info.str_desc_manufacturer) {
        printf("Manufacturer ");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        printf("Product      ");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        printf("S/N ID       ");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    printf("Speed mode   %s\n",
            dev_info.speed == USB_SPEED_LOW ? "low" : "full");
    printf("Config value %d\n", dev_info.bConfigurationValue);
    usb_print_device_descriptor(dev_desc);
    usb_print_config_descriptor(cfg_desc, NULL);
}

static esp_err_t usbd_common_init() {
    // see esp-idf-4.4/components/tinyusb/additions/src/usb_descriptors.c
    // see esp_tinyusb/usb_descriptors.c
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
    return err;
}

// USB Host library daemon task
static void usb_lib_task(void *arg) {
    uint32_t flags;
    const usb_host_config_t host_conf = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    if (( ctx.err = usb_host_install(&host_conf) )) {
        ctx.running = false;
        goto exit;
    }
    release(ctx.usblib_sem); // host library is installed
    msleep(TIMEOUT_IDLE); // let client task spin up
    bool has_clients = ctx.running, has_devices = ctx.running;
    while (has_clients || has_devices) {
        if (!ctx.running) {
            usb_host_lib_info_t info;
            if (usb_host_lib_info(&info) == ESP_ERR_INVALID_STATE) break;
            has_clients = info.num_clients;
            has_devices = info.num_devices;
            ESP_LOGI(TAG, "USB LIB devices %d clients %d",
                    info.num_devices, info.num_clients);
        }
        usb_host_lib_handle_events(pdMS_TO_TICKS(TIMEOUT_LOOP), &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "USB LIB all clients deregistered");
            usb_host_device_free_all();
            has_clients = false;
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB LIB all devices freed");
            has_devices = false;
        }
    }
    ESP_LOGI(TAG, "USB LIB no more clients and devices");
    usb_host_uninstall();
exit:
    release(ctx.usblib_sem); // host library is uninstalled
    vTaskDelete(NULL);
}

static esp_err_t usbh_common_init() {
    if (ctx.running) return ESP_OK;
    if (!ctx.usblib_sem) ctx.usblib_sem = xSemaphoreCreateBinary();
    if (!ctx.client_sem) ctx.client_sem = xSemaphoreCreateBinary();
    ctx.running = true;
    acquire(ctx.usblib_sem, 1); // make sure semaphores are taken
    acquire(ctx.client_sem, 1);
    if (xTaskCreate(usb_lib_task, "USB-LIB", 4096, NULL, 10, NULL) != pdPASS) {
        ctx.err = ESP_ERR_NO_MEM;
        ctx.running = false;
    }
    return ctx.err;
}

static esp_err_t usbh_common_exit() {
    ctx.running = false;
    if (!acquire(ctx.client_sem, TIMEOUT_WAIT))
        return ctx.err ?: ESP_ERR_TIMEOUT;
    if (!acquire(ctx.usblib_sem, TIMEOUT_WAIT)) {
        ESP_LOGE(TAG, "USB LIB stop failed");
        if (!ctx.err) ctx.err = ESP_ERR_TIMEOUT;
    }
    return ctx.err;
}

/******************************************************************************
 * USBMode: Serial JTAG
 */

static esp_err_t serial_jtag_init() {
#ifndef SOC_USB_SERIAL_JTAG_SUPPORTED
    return ESP_ERR_NOT_SUPPORTED;
#else
    usb_serial_jtag_driver_config_t conf = \
        USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&conf);
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
#endif
}

static esp_err_t serial_jtag_exit() {
#ifndef SOC_USB_SERIAL_JTAG_SUPPORTED
    return ESP_ERR_NOT_SUPPORTED;
#else
    return usb_serial_jtag_driver_uninstall();
#endif
}

/******************************************************************************
 * USBMode: CDC Device
 */

#ifdef CONFIG_USB_CDC_DEVICE_SERIAL
static void cdc_device_cb(int itf, cdcacm_event_t *event) {
    static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
    if (event->type == CDC_EVENT_RX) {
        size_t size = 0;
        esp_err_t err = tinyusb_cdcacm_read(itf, buf, sizeof(buf) - 1, &size);
        if (err) {
            ESP_LOGE(TAG, "Device CDC read error %s", esp_err_to_name(err));
        } else {
            ESP_LOGD(TAG, "Device CDC got data[%u]", size);
            ESP_LOG_BUFFER_HEXDUMP(TAG, buf, size, ESP_LOG_DEBUG);
            tinyusb_cdcacm_write_queue(itf, buf, size); // echo
            tinyusb_cdcacm_write_flush(itf, 0);
        }
    } else if (event->type == CDC_EVENT_RX_WANTED_CHAR) {
        ESP_LOGI(TAG, "Device CDC wanted char %c",
                event->rx_wanted_char_data.wanted_char);
    } else if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
        ESP_LOGI(TAG, "Device CDC line state DTR: %d, RTS: %d",
                event->line_state_changed_data.dtr,
                event->line_state_changed_data.rts);
    } else if (event->type == CDC_EVENT_LINE_CODING_CHANGED) {
        const cdc_line_coding_t *ptr = \
            event->line_coding_changed_data.p_line_coding;
        ESP_LOGI(TAG, "Device CDC line coding: %u,%u%c%c",
                ptr->bit_rate, ptr->data_bits,
                "NOEMS"[ptr->parity], "1H2"[ptr->stop_bits]);
    }
}
#endif

static esp_err_t cdc_device_init() {
#ifndef CONFIG_USB_CDC_DEVICE
    return ESP_ERR_NOT_SUPPORTED;
#else
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
#endif
}

static esp_err_t cdc_device_exit() {
#if !defined(CONFIG_USB_CDC_DEVICE) || !defined(TARGET_IDF_5)
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t err = ESP_OK;
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
    if (!err) err = esp_tusb_deinit_console(TINYUSB_CDC_ACM_0);
#   endif
    if (!err) err = tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0);
    if (!err) err = tinyusb_driver_uninstall();
    return err;
#endif
}

/******************************************************************************
 * USBMode: CDC Host
 */

#ifdef CONFIG_USB_CDC_HOST

static bool cdc_acm_rx_cb(const uint8_t *data, size_t size, void *arg) {
    ESP_LOGI(TAG, "Host CDC got data[%u]", size);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, size, ESP_LOG_INFO);
    return true;
}

static void cdc_acm_cb(const cdc_acm_host_dev_event_data_t *event, void *arg) {
    uint32_t vp;
    switch (event->type) {
        case CDC_ACM_HOST_ERROR:
            ESP_LOGE(TAG, "Host CDC error %d", event->data.error);
            break;
        case CDC_ACM_HOST_SERIAL_STATE:
            ESP_LOGI(TAG, "Host CDC got serial state notification 0x%04X",
                    event->data.serial_state.val);
            break;
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            if (( vp = usb_dev_vid_pid(event->data.cdc_hdl) )) {
                ESP_LOGI(TAG, "Host CDC lost device %s", vid_pid_str(vp));
            } else {
                ESP_LOGI(TAG, "Host CDC lost device");
            }
            cdc_acm_host_close(event->data.cdc_hdl);
            break;
        default: ESP_LOGW(TAG, "Host CDC unhandled event: %d\n", event->type);
    }
}

static void cdc_host_cb(usb_device_handle_t dev) {
    print_devinfo(dev);
    const usb_device_desc_t *desc;
    if (!usb_host_get_device_descriptor(dev, &desc) && wanted_device(desc)) {
        ctx.vid_pid = desc->idVendor << 16 | desc->idProduct;
        release(ctx.client_sem); // notify host task to open device
    }
}

static void cdc_host_task(void *arg) {
    if (!acquire(ctx.usblib_sem, TIMEOUT_WAIT) || !ctx.running) goto exit;
    const cdc_acm_host_driver_config_t driver_conf = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 5,
        .xCoreID = tskNO_AFFINITY,
        .new_dev_cb = cdc_host_cb,
    };
    const cdc_acm_host_device_config_t device_conf = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .user_arg = NULL,
        .event_cb = cdc_acm_cb,
        .data_cb = cdc_acm_rx_cb
    };
    if (( ctx.err = cdc_acm_host_install(&driver_conf) )) {
        ctx.running = false;
        goto exit;
    }
    while (1) {
        if (!ctx.running) {
            ESP_LOGI(TAG, "Host CDC trying to uninstall client");
            if (!( ctx.err = cdc_acm_host_uninstall() )) break;
            ESP_LOGE(TAG, "Host CDC uninstall failed: continue running");
            ctx.running = true;
        }
        if (!acquire(ctx.client_sem, TIMEOUT_LOOP)) continue;
        ESP_LOGI(TAG, "Host CDC opening device %s", vid_pid_str(ctx.vid_pid));
        cdc_acm_dev_hdl_t cdc_dev;
        uint16_t v = ctx.vid_pid >> 16, p = ctx.vid_pid & 0xFFFF;
        if (( ctx.err = cdc_acm_host_open(v, p, 0, &device_conf, &cdc_dev) )) {
            ESP_LOGE(TAG, "Host CDC not opened: %s", esp_err_to_name(ctx.err));
            continue;
        }
        cdc_acm_host_desc_print(cdc_dev);
        cdc_acm_line_coding_t line_coding;
        if (!cdc_acm_host_line_coding_get(cdc_dev, &line_coding)) {
            ESP_LOGI(TAG, "Host CDC line coding: %u,%u%c%u",
                    line_coding.dwDTERate, line_coding.bDataBits,
                    "NOEMS"[line_coding.bParityType],
                    "1H2"[line_coding.bCharFormat]);
        }
        const uint8_t TXSTR[6] = "help\r\n";
        cdc_acm_host_data_tx_blocking(cdc_dev, TXSTR, 6, TIMEOUT_WAIT);
        msleep(TIMEOUT_WAIT);
        bool dtr = true, rts = false;
        cdc_acm_host_set_control_line_state(cdc_dev, dtr, rts);
    }
exit:
    release(ctx.client_sem); // client is deregistered
    vTaskDelete(NULL);
}

static esp_err_t cdc_host_init() {
    usbh_common_init();
    if (!ctx.err) {
        xTaskCreate(cdc_host_task, "USB-CDC Host", 4096, NULL, 6, NULL);
        msleep(TIMEOUT_IDLE); // let tasks run
    }
    return ctx.err;
}

static esp_err_t cdc_host_exit() { return usbh_common_exit(); }

#else // CONFIG_USB_CDC_HOST

static esp_err_t cdc_host_init() { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t cdc_host_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif

/******************************************************************************
 * USBMode: MSC Device
 */

#if defined(CONFIG_USB_MSC_DEVICE) && defined(TARGET_IDF_4)

#   define CHECK_LUN(lun, ret)                                              \
    {                                                                       \
        if (ctx.state != MSC_DEVICE) return ret;                            \
        if ((lun) >= NUM_DISK) {                                            \
            ESP_LOGE(TAG, "%s invalid lun number %u", __func__, (lun));     \
            return ret;                                                     \
        }                                                                   \
        if (ctx.info[(lun)].pdrv == FF_DRV_NOT_USED) {                      \
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
    if (lun < NUM_DISK && ctx.info[lun].pdrv == FF_DRV_NOT_USED)
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    CHECK_LUN(lun, false);
    filesys_acquire(ctx.info[lun].isffs, TIMEOUT_WAIT);
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *blkcnt, uint16_t *blksize) {
    CHECK_LUN(lun,);
    *blkcnt = ctx.info[lun].blkcnt;
    *blksize = ctx.info[lun].blksize;
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
            filesys_acquire(ctx.info[lun].isffs, 1);
        } else {
            filesys_release(ctx.info[lun].isffs);
        }
    }
    return true; NOTUSED(pc);
}

int32_t tud_msc_read10_cb(
    uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size
) {
    CHECK_LUN(lun, -1);
    esp_err_t err;
    size_t ssize = ctx.info[lun].blksize;
    size_t addr = lba * ssize + offset;
    size_t bcnt = size / (ssize ?: 1);
    if (!ssize || addr  % ssize || size % ssize) {
        ESP_LOGE(TAG, "Invalid lba(%u) offset(%u) size(%u) ssize(%u)",
                lba, offset, size, ssize);
        err = ESP_ERR_INVALID_ARG;
    } else if (ctx.info[lun].isffs) {
        err = wl_read(ctx.info[lun].wlhdl, addr, buffer, size);
    } else {
        err = sdmmc_read_sectors(ctx.info[lun].card, buffer, lba, bcnt);
    }
    return err ? -1 : size;
}

int32_t tud_msc_write10_cb(
    uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size
) {
    CHECK_LUN(lun, -1);
    esp_err_t err;
    size_t ssize = ctx.info[lun].blksize;
    size_t addr = lba * ssize + offset;
    size_t bcnt = size / (ssize ?: 1);
    if (!ssize || addr % ssize || size % ssize) {
        ESP_LOGE(TAG, "Invalid lba(%u) offset(%u) size(%u) ssize(%u)",
                lba, offset, size, ssize);
        err = ESP_ERR_INVALID_ARG;
    } else if (!ctx.info[lun].isffs) {
        err = sdmmc_write_sectors(ctx.info[lun].card, buffer, lba, bcnt);
    } else if (( err = wl_erase_range(ctx.info[lun].wlhdl, addr, size) )) {
        ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(err));
    } else {
        err = wl_write(ctx.info[lun].wlhdl, addr, buffer, size);
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

static esp_err_t msc_device_init() {
#ifndef CONFIG_USB_MSC_DEVICE
    return ESP_ERR_NOT_SUPPORTED;
#else
    filesys_ffs_info(ctx.info + 0);
#   if NUM_DISK == 1
    if (ctx.info[0].pdrv == FF_DRV_NOT_USED)
#   endif
        filesys_sdfs_info(ctx.info + NUM_DISK - 1);

    // see esp-idf-5.2/examples/peripherals/usb/device/tusb_msc
    // see esp-iot-solution/examples/usb/device/usb_msc_wireless_disk
    esp_err_t err = ESP_ERR_INVALID_STATE;
    LOOPN(i, NUM_DISK) {
        // only mounted FAT file system has drive number
        if (ctx.info[i].pdrv == FF_DRV_NOT_USED) continue;
#   ifdef TARGET_IDF_4
        err = ESP_OK; // at least one drive is mounted
#   else // TARGET_IDF_5
        if (ctx.info[i].isffs) {
            const tinyusb_msc_spiflash_config_t conf = {
                .wl_handle = ctx.info[i].wlhdl
            };
            err = tinyusb_msc_storage_init_spiflash(&conf);
        } else {
            const tinyusb_msc_sdmmc_config_t conf = {
                .card = ctx.info[i].card
            };
            err = tinyusb_msc_storage_init_sdmmc(&conf);
        }
        if (err) break;
        if (i == NUM_DISK - 1) err = tinyusb_msc_storage_mount("/usb");
#   endif // TARGET_IDF_4
    }
    return err ?: usbd_common_init();
#endif
}

static esp_err_t msc_device_exit() {
#if !defined(CONFIG_USB_MSC_DEVICE) || !defined(TARGET_IDF_5)
    return ESP_ERR_NOT_SUPPORTED;
#else
    tinyusb_msc_storage_deinit();
    return tinyusb_driver_uninstall();
#endif
}

/******************************************************************************
 * USBMode: MSC Host
 */

#ifdef CONFIG_USB_MSC_HOST

static void msc_host_cb(const msc_host_event_t *event, void *arg) {
    uint32_t vp;
    msc_host_device_handle_t msc_dev;
    switch (event->event) {
        case MSC_DEVICE_CONNECTED:
            ctx.address = event->device.address;
            release(ctx.client_sem); // notify host task to open device
            break;
        case MSC_DEVICE_DISCONNECTED:
            msc_dev = event->device.handle;
            if (( vp = usb_dev_vid_pid(msc_dev->handle) )) {
                ESP_LOGI(TAG, "Host MSC lost device %s", vid_pid_str(vp));
            } else {
                ESP_LOGI(TAG, "Host MSC lost device");
            }
            msc_host_vfs_unregister(ctx.vfs_hdl);
            msc_host_uninstall_device(msc_dev);
            ctx.vfs_hdl = NULL;
            break;
        default: ESP_LOGW(TAG, "Host MSC unhandled event: %d\n", event->event);
    }
}

static void msc_host_task(void *arg) {
    if (!acquire(ctx.usblib_sem, TIMEOUT_WAIT) || !ctx.running) goto exit;
    const msc_host_driver_config_t driver_conf = {
        .create_backround_task = true,
        .stack_size = 4096,
        .task_priority = 5,
        .core_id = tskNO_AFFINITY,
        .callback = msc_host_cb,
    };
    const esp_vfs_fat_mount_config_t mount_conf = {
        .format_if_mount_failed = false,
        .max_files = 3,
        .allocation_unit_size = 1024,
    };
    if (( ctx.err = msc_host_install(&driver_conf) )) {
        ctx.running = false;
        goto exit;
    }
    while (1) {
        if (!ctx.running) {
            ESP_LOGI(TAG, "Host MSC trying to uninstall client");
            if (!( ctx.err = msc_host_uninstall() )) break;
            ESP_LOGE(TAG, "Host MSC uninstall failed: continue running");
            ctx.running = true;
        }
        if (!acquire(ctx.client_sem, TIMEOUT_LOOP)) continue;
        ESP_LOGI(TAG, "Host MSC opening device %d", ctx.address);
        msc_host_device_handle_t msc_dev;
        if (( ctx.err = msc_host_install_device(ctx.address, &msc_dev) )) {
            ESP_LOGE(TAG, "Host MSC not opened: %s", esp_err_to_name(ctx.err));
            continue;
        }
        msc_host_device_info_t info;
        if (!msc_host_get_device_info(msc_dev, &info)) {
            printf("Capacity     %s\nSector size  %u\nSector count 0x%08X\n",
                format_size((uint64_t)info.sector_size * info.sector_count, 0),
                info.sector_size, info.sector_count);
        }
        print_devinfo(msc_dev->handle);
        if (ctx.vfs_hdl) {
            msc_host_uninstall_device(msc_dev);
            continue;
        }
        msc_host_vfs_handle_t *pvfs = (msc_host_vfs_handle_t *)&ctx.vfs_hdl;
        ctx.err = msc_host_vfs_register(msc_dev, MSC_MP, &mount_conf, pvfs);
        if (ctx.err) {
            ESP_LOGE(TAG, "Host MSC not mount: %s", esp_err_to_name(ctx.err));
        } else {
            ESP_LOGI(TAG, "Host MSC mounted to %s", MSC_MP);
        }
    }
exit:
    release(ctx.client_sem); // client is deregistered
    vTaskDelete(NULL);
}

static esp_err_t msc_host_init() {
    usbh_common_init();
    if (!ctx.err) {
        xTaskCreate(msc_host_task, "USB-MSC Host", 4096, NULL, 6, NULL);
        msleep(TIMEOUT_IDLE);
    }
    return ctx.err;
}

static esp_err_t msc_host_exit() { return usbh_common_exit(); }

#else // CONFIG_USB_MSC_HOST

static esp_err_t msc_host_init() { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t msc_host_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif

/******************************************************************************
 * USBMode: HID Device
 */

static esp_err_t hid_device_init() {
#ifndef CONFIG_USB_HID_DEVICE
    return ESP_ERR_NOT_SUPPORTED;
#else
    return ESP_ERR_NOT_SUPPORTED; // TODO
#endif
}

/******************************************************************************
 * USBMode: HID Host
 */

static esp_err_t hid_host_init() {
#ifndef CONFIG_USB_HID_HOST
    return ESP_ERR_NOT_SUPPORTED;
#else
    return ESP_ERR_NOT_SUPPORTED; // TODO
#endif
}

/******************************************************************************
 * USBMode APIs
 */

static struct {
    usbmode_t mode;
    esp_err_t (*init)();
    esp_err_t (*exit)();
} modes[] = {
    { SERIAL_JTAG,  serial_jtag_init,   serial_jtag_exit },
    { CDC_DEVICE,   cdc_device_init,    cdc_device_exit },
    { CDC_HOST,     cdc_host_init,      cdc_host_exit },
    { MSC_DEVICE,   msc_device_init,    msc_device_exit },
    { MSC_HOST,     msc_host_init,      msc_host_exit },
    { HID_DEVICE,   hid_device_init,    NULL },
    { HID_HOST,     hid_host_init,      NULL },
};

static const char * usbmode_str(usbmode_t mode) {
    if (mode == -1 && ctx.state >= 0) mode = ctx.state;
    switch (mode) {
        CASESTR(SERIAL_JTAG, 0);
        CASESTR(CDC_DEVICE, 0);
        CASESTR(CDC_HOST, 0);
        CASESTR(MSC_DEVICE, 0);
        CASESTR(MSC_HOST, 0);
        CASESTR(HID_DEVICE, 0);
        CASESTR(HID_HOST, 0);
        default: if (mode != -1) return "Unknown";
    }
    esp_err_t err = -ctx.state;
    switch (err) {
        CASESTR(ESP_ERR_DISABLED, 8);
        CASESTR(ESP_ERR_NOT_INITED, 8);
        CASESTR(ESP_ERR_PENDING_REBOOT, 8);
        default: return esp_err_to_name(err);
    }
}

esp_err_t usbmode_switch(usbmode_t mode, bool restart) {
    esp_err_t err = ESP_OK;
    if (mode == ctx.state) return err;
    bool exited = ctx.state == -ESP_ERR_DISABLED || \
                  ctx.state == -ESP_ERR_NOT_INITED;
    LOOPN(i, LEN(modes)) {
        if (mode != modes[i].mode) continue;
        LOOPN(j, LEN(modes)) {
            if (ctx.state != modes[j].mode || !modes[j].exit) continue;
            if (( err = modes[j].exit() )) {
                ESP_LOGE(TAG, "USB mode %s exit failed: %s\n",
                        usbmode_str(ctx.state), esp_err_to_name(err));
            } else {
                exited = true;
            }
        }
        config_set("app.usb.mode", usbmode_str(modes[i].mode));
        if (!exited) {
            if (restart) esp_restart(); // reboot here
            ctx.state = -ESP_ERR_PENDING_REBOOT;
            ESP_LOGI(TAG, "USB mode set to %s (pending)", usbmode_str(mode));
            return err;
        }
        if (!( err = modes[i].init ? modes[i].init() : ESP_OK )) {
            ctx.state = modes[i].mode;
            ESP_LOGI(TAG, "USB mode set to %s", usbmode_str(mode));
        } else {
            ESP_LOGE(TAG, "USB mode set to %s failed: %s",
                    Config.app.USB_MODE, esp_err_to_name(err));
            ctx.state = -err;
        }
        return err;
    }
    ESP_LOGE(TAG, "Invalid USB mode %d: %s\n", mode, usbmode_str(mode));
    return ESP_ERR_NOT_FOUND;
}

void usbmode_initialize() {
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = -ESP_ERR_NOT_INITED;
    LOOPN(i, NUM_DISK) { ctx.info[i].pdrv = FF_DRV_NOT_USED; }
    LOOPN(i, LEN(modes)) {
        if (strcmp(usbmode_str(modes[i].mode), Config.app.USB_MODE)) continue;
        usbmode_switch(modes[i].mode, false);
        return;
    }
    if (!strlen(Config.app.USB_MODE)) {
        ESP_LOGW(TAG, "USB is software blocked");
        ctx.state = -ESP_ERR_DISABLED;
    } else {
        ESP_LOGE(TAG, "Unknown USB mode. This should not happen!");
    }
}

void usbmode_status() {
    printf("USB mode is %s (%d)\n", usbmode_str(-1), ABS(ctx.state));
    if (
        ctx.state == CDC_HOST ||
        ctx.state == MSC_HOST ||
        ctx.state == HID_HOST
    ) {
        usb_host_lib_info_t info;
        esp_err_t err = usb_host_lib_info(&info);
        if (err) {
            printf("Could not get host info: %s", esp_err_to_name(err));
            return;
        }
        printf("%d devices, %d clients\n", info.num_devices, info.num_clients);
    }
#ifdef CONFIG_USB_CDC_DEVICE
    if (ctx.state == CDC_DEVICE) {
#   ifdef CONFIG_USB_CDC_DEVICE_SERIAL
        puts("Running as CDC serial device");
#   endif
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
        puts("Running as CDC console device");
#   endif
    }
#endif
#ifdef CONFIG_USB_MSC_DEVICE
    if (ctx.state == MSC_DEVICE) {
        LOOPN(i, NUM_DISK) {
            if (ctx.info[i].pdrv == FF_DRV_NOT_USED) {
                printf("Disk[%d]: not mouned / supported\n", i);
            } else {
                printf("Disk[%d]: pdrv=%u, ssize=%u, total=%s\n",
                    i, ctx.info[i].pdrv, ctx.info[i].blksize,
                    format_size(ctx.info[i].total, false));
            }
        }
    }
#endif
#ifdef CONFIG_USB_HID_DEVICE
    if (ctx.state == HID_DEVICE) {
        puts("TODO");
    }
#endif
}

#else // CONFIG_USE_USB

esp_err_t usbmode_switch(usbmode_t m, bool b) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(m); NOTUSED(b);
}

void usbmode_initialize() {}

void usbmode_status() {}

#endif // CONFIG_USE_USB
