/* 
 * File: usbmode.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#include "usbmode.h"
#include "config.h"

#include "soc/soc_caps.h"

#if SOC_USB_OTG_SUPPORTED && defined(CONFIG_USE_USB)

#include "tinyusb.h"
#include "../include_private/usb_descriptors.h"

#ifdef CONFIG_USB_CDC_DEVICE
#   include "tusb_cdc_acm.h"
#endif

#ifdef CONFIG_USB_CDC_HOST
#   include "usb/cdc_acm_host.h"
#endif

static const char *TAG = "USBMode";

#ifdef CONFIG_USB_CDC_DEVICE_SERIAL
void cdc_device_cb(int itf, cdcacm_event_t *event) {
    static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
    if (event->type == CDC_EVENT_RX) {
        size_t size = 0;
        esp_err_t err = tinyusb_cdcacm_read(itf, buf, sizeof(buf) - 1, &size);
        if (err) {
            ESP_LOGE(TAG, "USB: dev CDC read error %s", esp_err_to_name(err));
        } else {
            ESP_LOGD(TAG, "USB: dev CDC got data[%u]", size);
            ESP_LOG_BUFFER_HEXDUMP(TAG, buf, size, ESP_LOG_DEBUG);
            tinyusb_cdcacm_write_queue(itf, buf, size); // echo
            tinyusb_cdcacm_write_flush(itf, 0);
        }
    } else if (event->type == CDC_EVENT_LINE_STATE_CHANGED) {
        ESP_LOGI(TAG, "USB: dev CDC line state DTR: %d, RTS: %d",
                event->line_state_changed_data.dtr,
                event->line_state_changed_data.rts);
    }
}
#endif

#ifdef CONFIG_USB_CDC_HOST
void cdc_host_cb(usb_device_handle_t usb_dev) {
    usb_device_info_t dev_info;
    const usb_device_desc_t *dev_desc;
    const usb_config_desc_t *cfg_desc;
    esp_err_t err = usb_host_device_info(usb_dev, &dev_info);
    if (!err) err = usb_host_get_device_descriptor(usb_dev, &dev_desc);
    if (!err) err = usb_host_get_active_config_descriptor(usb_dev, &cfg_desc);
    ESP_LOGI(TAG, "\t%s speed", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    // TODO: print more dev_info
    usb_print_device_descriptor(dev_desc);
    usb_print_config_descriptor(cfg_desc, NULL);
    if (dev_info.str_desc_manufacturer) {
        ESP_LOGI(TAG, "Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        ESP_LOGI(TAG, "Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        ESP_LOGI(TAG, "Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    // TODO: do not open CDC device in this context
}

// USB Host library daemon task
static void usb_lib_task(void *arg) {
    while (1) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            ESP_ERROR_CHECK(usb_host_device_free_all());
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
            ESP_LOGI(TAG, "USB: All devices freed");
    }
}
#endif

esp_err_t cdc_device_init() {
#ifndef CONFIG_USB_CDC_DEVICE
    return ESP_ERR_NOT_SUPPORTED;
#else
    // see esp-idf/components/tinyusb/additions/src/usb_descriptors.c
    int ver[3];
    esp_err_t err;
    if (parse_all(Config.info.VER, ver, 3) >= 2)
        descriptor_kconfig.bcdDevice = (uint8_t)(ver[0] << 8) | (uint8_t)ver[1];
    if (strlen(Config.info.UID))
        descriptor_str_kconfig[3] = Config.info.UID;
    tinyusb_config_t tusb_conf = {
        .external_phy = false,
        .descriptor = &descriptor_kconfig,
        .string_descriptor = descriptor_str_kconfig,
    };
    if (( err = tinyusb_driver_install(&tusb_conf) )) return err;
    LOOP(i, 1, LEN(descriptor_str_kconfig)) {
        ESP_LOGI(TAG, "desc[%d] = %s", i, descriptor_str_kconfig[i]);
    }
    tinyusb_config_cdcacm_t acm_conf = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
#   ifdef CONFIG_USB_CDC_DEVICE_SERIAL
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
        .callback_rx = &cdc_device_cb,
        .callback_line_state_changed = &cdc_device_cb,
#   endif
    };
    if (( err = tusb_cdc_acm_init(&acm_conf) )) return err;
#   ifdef CONFIG_USB_CDC_DEVICE_CONSOLE
    if (( err = esp_tusb_init_console(TINYUSB_CDC_ACM_0) )) return err;
    ESP_LOGI(TAG, "Running as CDC console device");
#   else
    ESP_LOGI(TAG, "Running as CDC serial device");
#   endif
    return err;
#endif
}

esp_err_t hid_device_init() {
    return ESP_ERR_NOT_SUPPORTED; // TODO
}

esp_err_t cdc_host_init() {
#ifndef CONFIG_USB_CDC_HOST
    return ESP_ERR_NOT_SUPPORTED;
#else
    const usb_host_config_t host_conf = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_conf);
    if (!err) err = cdc_acm_host_install(NULL);
    if (!err) err = cdc_acm_host_register_new_dev_callback(cdc_host_cb);
    if (!err) {
        xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 20, NULL);
        ESP_LOGI(TAG, "Running as CDC host");
    }
    return err;
#endif
}

esp_err_t msc_host_init() {
    return ESP_ERR_NOT_SUPPORTED; // TODO
}

static struct {
    usbmode_t mode;
    esp_err_t (*init)();
} modes[] = {
    { CDC_DEVICE, cdc_device_init },
    { HID_DEVICE, hid_device_init },
    { CDC_HOST, cdc_host_init },
    { MSC_HOST, msc_host_init },
};

#define ESP_ERR_DISABLED        0x501
#define ESP_ERR_NOT_INITED      0x502
#define ESP_ERR_PENDING_REBOOT  0x503

static int state = -ESP_ERR_NOT_INITED;

const char * usbmode_str(usbmode_t mode) {
    if (mode == -1 && state >= 0) mode = state;
    switch (mode) {
        CASESTR(CDC_DEVICE, 0);
        CASESTR(HID_DEVICE, 0);
        CASESTR(CDC_HOST, 0);
        CASESTR(MSC_HOST, 0);
        default: if (mode != -1) return "Unknown";
    }
    esp_err_t err = -state;
    switch (err) {
        CASESTR(ESP_ERR_DISABLED, 8);
        CASESTR(ESP_ERR_NOT_INITED, 8);
        CASESTR(ESP_ERR_PENDING_REBOOT, 8);
        default: return esp_err_to_name(err);
    }
}

esp_err_t usbmode_switch(usbmode_t mode, bool restart) {
    LOOPN(i, LEN(modes)) {
        if (mode != modes[i].mode) continue;
        if (mode != state) {
            config_set("app.usb.mode", usbmode_str(modes[i].mode));
            if (restart) esp_restart(); // reboot here
            state = -ESP_ERR_PENDING_REBOOT;
        }
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void usbmode_initialize() {
    if (!strlen(Config.app.USB_MODE)) {
        ESP_LOGW(TAG, "USB is software blocked");
        state = -ESP_ERR_DISABLED;
        return;
    }
    LOOPN(i, LEN(modes)) {
        if (strcmp(usbmode_str(modes[i].mode), Config.app.USB_MODE)) continue;
        esp_err_t err = modes[i].init();
        if (!err) {
            state = modes[i].mode;
        } else {
            ESP_LOGE(TAG, "Switch USB mode to %s failed: %s",
                    Config.app.USB_MODE, esp_err_to_name(err));
            state = -err;
        }
        return;
    }
    ESP_LOGE(TAG, "Unknown USB mode. This should not happen!");
}

#else // CONFIG_USE_USB

const char * usbmode_str(usbmode_t m) { return "Unknown"; NOTUSED(m); }

esp_err_t usbmode_switch(usbmode_t m, bool b) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(m); NOTUSED(b);
}

void usbmode_initialize() {}

#endif // CONFIG_USE_USB
