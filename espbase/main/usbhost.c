/*
 * File: usbhost.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-25 17:23:12
 */

#define DUAL_TUSB
#include "usbmode.h"

#ifdef CONFIG_BASE_USE_USB

#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#ifdef CONFIG_BASE_USB_CDC_HOST
#   include "usb/cdc_acm_host.h"
#endif

#ifdef CONFIG_BASE_USB_MSC_HOST
#   include "msc_host.h"
#   include "msc_host_vfs.h"
#endif

#ifdef CONFIG_BASE_USB_HID_HOST
#   include "usb/hid_host.h"
#endif

#define TIMEOUT_IDLE        10
#define TIMEOUT_LOOP        50
#define TIMEOUT_WAIT        200
#define BIT_USBLIB_INIT     BIT0
#define BIT_USBLIB_EXIT     BIT1
#define BIT_CLIENT_INIT     BIT2
#define BIT_CLIENT_EXIT     BIT3
#define BIT_DEVICE_INIT     BIT4
#define BIT_DEVICE_EXIT     BIT5

static const char *TAG = "USBHost";

static struct {
    esp_err_t err;
    bool running;
    void * vfs_hdl;
    union {
        void * dev_hdl;
        uint8_t address;
        uint32_t vid_pid;
    };
    EventGroupHandle_t evtgrp;
} ctx = { ESP_OK, false, NULL, { 0 }, NULL };

/******************************************************************************
 * Helper functions
 */

void usbhost_status(usbmode_t mode) {
    usb_host_lib_info_t info;
    esp_err_t err = usb_host_lib_info(&info);
    if (err) {
        printf("Could not get host info: %s", esp_err_to_name(err));
        return;
    }
    printf("%d devices, %d clients\n", info.num_devices, info.num_clients);
    NOTUSED(mode);
}

static bool waitBits(EventBits_t bits, uint32_t ms) {
    // ClearOnExit = true, WaitForAllBits = false
    return ctx.evtgrp ? xEventGroupWaitBits(
        ctx.evtgrp, bits, pdTRUE, pdFALSE, TIMEOUT(ms)) & bits : false;
}

static bool getBits(EventBits_t bits) {
    return ctx.evtgrp ? xEventGroupGetBits(ctx.evtgrp) & bits : false;
}

static bool setBits(EventBits_t bits) {
    return ctx.evtgrp ? xEventGroupSetBits(ctx.evtgrp, bits) : false;
}

static void clearBits(EventBits_t bits) {
    if (ctx.evtgrp) xEventGroupClearBits(ctx.evtgrp, bits);
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
        printf("SerialNumber ");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    printf("Speed mode   %s\nbConfigValue %d\n",
            dev_info.speed == USB_SPEED_LOW ? "Low" : "Full",
            dev_info.bConfigurationValue);
    usb_print_device_descriptor(dev_desc);
    usb_print_config_descriptor(cfg_desc, NULL);
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
    setBits(BIT_USBLIB_INIT); // host library is installed
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
    setBits(BIT_USBLIB_EXIT); // host library is uninstalled
    vTaskDelete(NULL);
}

static esp_err_t usbh_common_init(void (*client)(void *), const char *cname) {
    if (ctx.running) return ESP_OK;
    if (!ctx.evtgrp) ctx.evtgrp = xEventGroupCreate();
    clearBits(0xFFF); // EventBits_t accepts 24 bits
    ctx.running = true;
    if (xTaskCreate(usb_lib_task, "USB-LIB", 4096, NULL, 10, NULL) != pdPASS) {
        ctx.err = ESP_ERR_NO_MEM;
        ctx.running = false;
    }
    if (!ctx.err) {
        char taskname[16];
        snprintf(taskname, sizeof(taskname), "USB-%s", cname);
        xTaskCreate(client, taskname, 4096, NULL, 6, NULL);
        waitBits(BIT_CLIENT_INIT, TIMEOUT_IDLE + TIMEOUT_WAIT);
    }
    return ctx.err;
}

static esp_err_t usbh_common_exit() {
    ctx.running = false;
    if (!waitBits(BIT_CLIENT_EXIT, TIMEOUT_WAIT))
        return ctx.err ?: ESP_ERR_TIMEOUT;
    if (!waitBits(BIT_USBLIB_EXIT, TIMEOUT_WAIT)) {
        ESP_LOGE(TAG, "USB LIB stop failed");
        if (!ctx.err) ctx.err = ESP_ERR_TIMEOUT;
    }
    return ctx.err;
}

/******************************************************************************
 * USBMode: CDC Host
 */

#ifdef CONFIG_BASE_USB_CDC_HOST

static const char * CDC = "CDC Host";

static bool cdc_acm_rx_cb(const uint8_t *data, size_t size, void *arg) {
    ESP_LOGI(TAG, "%s got data[%u]", CDC, size);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, size, ESP_LOG_INFO);
    return true;
}

static void cdc_acm_cb(const cdc_acm_host_dev_event_data_t *event, void *arg) {
    uint32_t vp;
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "%s error %d", CDC, event->data.error);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        ESP_LOGI(TAG, "%s got serial state notification 0x%04X",
                CDC, event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        if (( vp = usb_dev_vid_pid(event->data.cdc_hdl) )) {
            ESP_LOGI(TAG, "%s lost device %s", CDC, vid_pid_str(vp));
        } else {
            ESP_LOGI(TAG, "%s lost device", CDC);
        }
        cdc_acm_host_close(event->data.cdc_hdl);
        setBits(BIT_DEVICE_EXIT);
        break;
    default: ESP_LOGW(TAG, "%s unhandled event: %d", CDC, event->type);
    }
}

bool usbdev_interest(const void *desc); // implemented in usbdev.c

static void cdc_host_cb(usb_device_handle_t dev) {
    const usb_device_desc_t *desc;
    if (!usb_host_get_device_descriptor(dev, &desc) && usbdev_interest(desc)) {
        ctx.vid_pid = desc->idVendor << 16 | desc->idProduct;
        setBits(BIT_DEVICE_INIT);
    } else {
        print_devinfo(dev);
    }
}

static void cdc_host_task(void *arg) {
    if (!waitBits(BIT_USBLIB_INIT, TIMEOUT_WAIT) || !ctx.running) goto exit;
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
    setBits(BIT_CLIENT_INIT);
    while (1) {
        if (!ctx.running) {
            ESP_LOGI(TAG, "%s trying to uninstall client", CDC);
            if (!( ctx.err = cdc_acm_host_uninstall() )) break;
            ESP_LOGE(TAG, "%s uninstall failed: continue running", CDC);
            ctx.running = true;
        }
        if (!waitBits(BIT_DEVICE_INIT, TIMEOUT_LOOP)) continue;
        clearBits(BIT_DEVICE_EXIT);

        cdc_acm_dev_hdl_t dev;
        cdc_acm_line_coding_t line_coding;
        uint16_t v = ctx.vid_pid >> 16, p = ctx.vid_pid & 0xFFFF;
        if (( ctx.err = cdc_acm_host_open(v, p, 0, &device_conf, &dev) )) {
            ESP_LOGE(TAG, "%s not opened: %s", CDC, esp_err_to_name(ctx.err));
            goto close;
        }
        if (( ctx.err = cdc_acm_host_line_coding_get(dev, &line_coding) )) {
            ESP_LOGE(TAG, "%s no devinfo: %s", CDC, esp_err_to_name(ctx.err));
            goto close;
        }

        ESP_LOGI(TAG, "%s opened device %s %u,%u%c%c",
                CDC, vid_pid_str(ctx.vid_pid),
                line_coding.dwDTERate,
                line_coding.bDataBits,
                "NOEMS"[line_coding.bParityType],
                "1H2"[line_coding.bCharFormat]);
        cdc_acm_host_desc_print(dev);

        msleep(TIMEOUT_WAIT);

        if (!getBits(BIT_DEVICE_EXIT)) {
            const uint8_t TXSTR[5] = "help\0";
            cdc_acm_host_data_tx_blocking(
                dev, TXSTR, sizeof(TXSTR), TIMEOUT_WAIT);
            ESP_LOGI(TAG, "%s sent message `%s`", CDC, (const char *)TXSTR);
        }

        msleep(TIMEOUT_WAIT);

        if (!getBits(BIT_DEVICE_EXIT)) {
            bool dtr = true, rts = false;
            cdc_acm_host_set_control_line_state(dev, dtr, rts);
            ESP_LOGI(TAG, "%s set DTR %d RTS %d", CDC, dtr, rts);
        }
        continue;
close:
        if (dev && !getBits(BIT_DEVICE_EXIT)) {
            cdc_acm_host_close(dev);
            setBits(BIT_DEVICE_EXIT);
        }
    }
exit:
    setBits(BIT_CLIENT_EXIT); // client is deregistered
    vTaskDelete(NULL);
}

esp_err_t cdc_host_init() { return usbh_common_init(cdc_host_task, CDC); }
esp_err_t cdc_host_exit() { return usbh_common_exit(); }

#else // CONFIG_BASE_USB_CDC_HOST

esp_err_t cdc_host_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cdc_host_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_USB_MSC_HOST

/******************************************************************************
 * USBMode: MSC Host
 */

#ifdef CONFIG_BASE_USB_MSC_HOST

static const char * MSC = "MSC Host";
static const char * MMP = "/msc";

static void msc_host_cb(const msc_host_event_t *event, void *arg) {
    msc_host_device_info_t info;
    msc_host_device_handle_t dev;
    switch (event->event) {
    case MSC_DEVICE_CONNECTED:
        ctx.address = event->device.address;
        setBits(BIT_DEVICE_INIT);
        break;
    case MSC_DEVICE_DISCONNECTED:
        dev = event->device.handle;
        if (!msc_host_get_device_info(dev, &info)) {
            uint32_t vp = info.idVendor << 16 | info.idProduct;
            ESP_LOGI(TAG, "%s lost device %s", MSC, vid_pid_str(vp));
        } else {
            ESP_LOGI(TAG, "%s lost device", MSC);
        }
        if (ctx.vfs_hdl) {
            msc_host_vfs_unregister(ctx.vfs_hdl);
            ctx.vfs_hdl = NULL;
        }
        msc_host_uninstall_device(dev);
        setBits(BIT_DEVICE_EXIT);
        break;
    default: ESP_LOGW(TAG, "%s unhandled event: %d", MSC, event->event);
    }
}

static void msc_host_task(void *arg) {
    if (!waitBits(BIT_USBLIB_INIT, TIMEOUT_WAIT) || !ctx.running) goto exit;
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
    setBits(BIT_CLIENT_INIT);
    while (1) {
        if (!ctx.running) {
            ESP_LOGI(TAG, "%s trying to uninstall client", MSC);
            if (!( ctx.err = msc_host_uninstall() )) break;
            ESP_LOGE(TAG, "%s uninstall failed: continue running", MSC);
            ctx.running = true;
        }
        if (!waitBits(BIT_DEVICE_INIT, TIMEOUT_LOOP)) continue;
        clearBits(BIT_DEVICE_EXIT);

        msc_host_device_info_t info;
        msc_host_device_handle_t dev;
        if (( ctx.err = msc_host_install_device(ctx.address, &dev) )) {
            ESP_LOGE(TAG, "%s not opened: %s", MSC, esp_err_to_name(ctx.err));
            goto close;
        }
        if (( ctx.err = msc_host_get_device_info(dev, &info) )) {
            ESP_LOGE(TAG, "%s no devinfo: %s", MSC, esp_err_to_name(ctx.err));
            goto close;
        }

        ESP_LOGI(TAG, "%s opened device %d", MSC, ctx.address);
        if (wcslen(info.iManufacturer))
            wprintf(L"Manufacturer %ls\n", info.iManufacturer);
        if (wcslen(info.iProduct))
            wprintf(L"Product      %ls\n", info.iProduct);
        if (wcslen(info.iSerialNumber))
            wprintf(L"SerialNumber %ls\n", info.iSerialNumber);
        uint64_t cap = (uint64_t)info.sector_size * info.sector_count;
        printf("Total        %s\nSector       %u Bytes\nCount        0x%08X\n",
                format_size(cap, false), info.sector_size, info.sector_count);
        msc_host_print_descriptors(dev);

        if (ctx.vfs_hdl) goto close; // only one MSC device can be mounted
        msc_host_vfs_handle_t *pvfs = (msc_host_vfs_handle_t *)&ctx.vfs_hdl;
        if (( ctx.err = msc_host_vfs_register(dev, MMP, &mount_conf, pvfs) )) {
            const char *estr;
            switch (ctx.err) {
            case ESP_ERR_MSC_MOUNT_FAILED:  estr = "mount failed"; break;
            case ESP_ERR_MSC_FORMAT_FAILED: estr = "format failed"; break;
            case ESP_ERR_MSC_INTERNAL:      estr = "host internal error"; break;
            case ESP_ERR_MSC_STALL:         estr = "usb transfer stall"; break;
            default:                        estr = esp_err_to_name(ctx.err);
            }
            ESP_LOGE(TAG, "%s not mount: %s", MSC, estr);
            goto close;
        }
        ESP_LOGI(TAG, "%s mounted to %s", MSC, MMP);
        continue;
close:
        if (dev && !getBits(BIT_DEVICE_EXIT)) {
            msc_host_uninstall_device(dev);
            setBits(BIT_DEVICE_EXIT);
        }
    }
exit:
    setBits(BIT_CLIENT_EXIT); // client is deregistered
    vTaskDelete(NULL);
}

esp_err_t msc_host_init() { return usbh_common_init(msc_host_task, MSC); }
esp_err_t msc_host_exit() { return usbh_common_exit(); }

#else // CONFIG_BASE_USB_MSC_HOST

esp_err_t msc_host_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t msc_host_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_USB_MSC_HOST

/******************************************************************************
 * USBMode: HID Host
 */

#ifdef CONFIG_BASE_USB_HID_HOST

static const char * HID = "HID Host";

static const char * hid_protocol_str(hid_protocol_t proto) {
    if (proto == HID_PROTOCOL_NONE)     return "Generic";
    if (proto == HID_PROTOCOL_KEYBOARD) return "Keyboard";
    if (proto == HID_PROTOCOL_MOUSE)    return "Mouse";
    static char buf[4];
    snprintf(buf, sizeof(buf), "%d", proto);
    return buf;
}

static void hid_event_cb(
    hid_host_device_handle_t dev,
    const hid_host_interface_event_t event,
    void *arg
) {
    // see esp-idf-5.2/examples/peripherals/usb/host/hid
    hid_host_dev_info_t info;
    hid_host_dev_params_t params;
    if (hid_host_device_get_params(dev, &params)) return;
    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGD(TAG, "%s address %d transfer_error", HID, params.addr);
        return;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        if (!hid_host_get_device_info(dev, &info)) {
            ESP_LOGI(TAG, "%s lost device %s",
                     HID, vid_pid_str(info.VID << 16 | info.PID));
        } else {
            ESP_LOGI(TAG, "%s lost device", HID);
        }
        hid_host_device_close(dev);
        setBits(BIT_DEVICE_EXIT);
        return;
    default:
        ESP_LOGW(TAG, "%s unhandled event: %d", HID, event);
        return;
    }
    uint8_t data[64];
    size_t size = 0;
    hid_host_device_get_raw_input_report_data(dev, data, sizeof(data), &size);
    if (params.proto == HID_PROTOCOL_KEYBOARD) {
        hid_keybd_report_t *kbd = (hid_keybd_report_t *)data;
        if (size < sizeof(*kbd)) return;
        hid_report_t report = { .id = REPORT_ID_KEYBD, .keybd = *kbd };
        hid_report_send(HID_TARGET_SCN, &report);
        hid_handle_keybd(HID_TARGET_USB, kdb, NULL);
    } else if (params.proto == HID_PROTOCOL_MOUSE) {
        hid_mouse_report_t *mse = (hid_mouse_report_t *)data;
        if (size < sizeof(*mse)) return;
        hid_report_t report = { .id = REPORT_ID_MOUSE, .mouse = *mse };
        hid_report_send(HID_TARGET_SCN, &report);
        hid_handle_mouse(HID_TARGET_USB, mse, NULL, NULL);
    } else if (params.sub_class != HID_SUBCLASS_BOOT_INTERFACE) {
        int offset = printf("%s %s ", HID, hid_protocol_str(params.proto));
        if (offset > 0) hexdump(data, size, 80 - offset);
    }
}

static void hid_host_cb(
    hid_host_device_handle_t dev,
    const hid_host_driver_event_t event,
    void *arg
) {
    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED:
        ctx.dev_hdl = dev;
        setBits(BIT_DEVICE_INIT);
        break;
    default: ESP_LOGW(TAG, "%s unhandled event: %d", HID, event);
    }
}

static void hid_host_task(void *arg) {
    if (!waitBits(BIT_USBLIB_INIT, TIMEOUT_WAIT) || !ctx.running) goto exit;
    const hid_host_driver_config_t driver_conf = {
        .create_background_task = true,
        .stack_size = 4096,
        .task_priority = 5,
        .core_id = tskNO_AFFINITY,
        .callback = hid_host_cb,
    };
    const hid_host_device_config_t device_conf = { .callback = hid_event_cb };
    if (( ctx.err = hid_host_install(&driver_conf) )) {
        ctx.running = false;
        goto exit;
    }
    setBits(BIT_CLIENT_INIT);
    while (1) {
        if (!ctx.running) {
            ESP_LOGI(TAG, "%s trying to uninstall client", HID);
            if (!( ctx.err = hid_host_uninstall() )) break;
            ESP_LOGE(TAG, "%s uninstall failed: continue running", HID);
            ctx.running = true;
        }
        if (!waitBits(BIT_DEVICE_INIT, TIMEOUT_LOOP)) continue;
        clearBits(BIT_DEVICE_EXIT);

        hid_host_dev_info_t info;
        hid_host_dev_params_t params;
        hid_host_device_handle_t dev = ctx.dev_hdl;
        if (
            ( ctx.err = hid_host_device_get_params(dev, &params) ) ||
            ( ctx.err = hid_host_device_open(dev, &device_conf) )
        ) {
            ESP_LOGE(TAG, "%s not opened: %s", HID, esp_err_to_name(ctx.err));
            goto close;
        }
        if (( ctx.err = hid_host_get_device_info(dev, &info) )) {
            ESP_LOGE(TAG, "%s no devinfo: %s", HID, esp_err_to_name(ctx.err));
            goto close;
        }

        ESP_LOGI(TAG, "%s opened device %d", HID, params.addr);
        if (wcslen(info.iManufacturer))
            wprintf(L"Manufacturer %ls\n", info.iManufacturer);
        if (wcslen(info.iProduct))
            wprintf(L"Product      %ls\n", info.iProduct);
        if (wcslen(info.iSerialNumber))
            wprintf(L"SerialNumber %ls\n", info.iSerialNumber);
        printf("SubClass     %s\nProto        %s\n",
               params.sub_class ? "BOOT" : "", hid_protocol_str(params.proto));

        if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
            hid_class_request_set_protocol(dev, HID_REPORT_PROTOCOL_BOOT);
            if (params.proto == HID_PROTOCOL_KEYBOARD) {
                if (( ctx.err = hid_class_request_set_idle(dev, 0, 0) ))
                    goto close;
            }
        }
        if (( ctx.err = hid_host_device_start(dev) )) {
            ESP_LOGE(TAG, "%s not start: %s", HID, esp_err_to_name(ctx.err));
            goto close;
        }
        ESP_LOGI(TAG, "%s start awaiting interface events", HID);
        continue;
close:
        if (dev && !getBits(BIT_DEVICE_EXIT)) {
            hid_host_device_close(dev);
            setBits(BIT_DEVICE_EXIT);
        }
    }
exit:
    setBits(BIT_CLIENT_EXIT); // client is deregistered
    vTaskDelete(NULL);
}

esp_err_t hid_host_init() { return usbh_common_init(hid_host_task, HID); }
esp_err_t hid_host_exit() { return usbh_common_exit(); }

#else // CONFIG_BASE_USB_HID_HOST

esp_err_t hid_host_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t hid_host_exit() { return ESP_ERR_NOT_SUPPORTED; }

#endif // CONFIG_BASE_USB_MSC_HOST

#endif // CONFIG_BASE_USE_USB
