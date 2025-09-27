/* 
 * File: usbmode.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#include "usbmode.h"
#include "config.h"

#include "esp_system.h"

#ifdef CONFIG_BASE_USE_USB

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#   include "driver/usb_serial_jtag.h"
#endif

#define ESP_ERR_USBMODE_BASE            0x500
#define ESP_ERR_USBMODE_DISABLED        ( ESP_ERR_USBMODE_BASE + 1 )
#define ESP_ERR_USBMODE_NOT_INITED      ( ESP_ERR_USBMODE_BASE + 2 )
#define ESP_ERR_USBMODE_PENDING_REBOOT  ( ESP_ERR_USBMODE_BASE + 3 )

static const char *TAG = "USBMode";
static int state = -ESP_ERR_USBMODE_NOT_INITED;

/*
 * USBMode: Serial JTAG
 */

static esp_err_t serial_jtag_init() {
#ifndef SOC_USB_SERIAL_JTAG_SUPPORTED
    return ESP_ERR_NOT_SUPPORTED;
#else
#   ifndef IDF_TARGET_V4
    if (usb_serial_jtag_is_driver_installed()) return ESP_OK;
#   endif
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

/*
 * USBMode APIs
 */

// Implemented in usbhost.c
void usbhost_status(usbmode_t mode);
esp_err_t cdc_host_init();
esp_err_t cdc_host_exit();
esp_err_t msc_host_init();
esp_err_t msc_host_exit();
esp_err_t hid_host_init();
esp_err_t hid_host_exit();

// Implemented in usbdev.c
void usbdev_status(usbmode_t mode);
esp_err_t cdc_device_init();
esp_err_t cdc_device_exit();
esp_err_t msc_device_init();
esp_err_t msc_device_exit();
esp_err_t hid_device_init();
esp_err_t hid_device_exit();

static const struct {
    usbmode_t mode;
    esp_err_t (*init)(usbmode_t prev);
    esp_err_t (*exit)(usbmode_t next);
} modes[] = {
    { SERIAL_JTAG,  serial_jtag_init,   serial_jtag_exit },
    { CDC_DEVICE,   cdc_device_init,    cdc_device_exit },
    { MSC_DEVICE,   msc_device_init,    msc_device_exit },
    { HID_DEVICE,   hid_device_init,    hid_device_exit },
    { CDC_HOST,     cdc_host_init,      cdc_host_exit },
    { MSC_HOST,     msc_host_init,      msc_host_exit },
    { HID_HOST,     hid_host_init,      hid_host_exit },
};

static const char * usbmode_str(usbmode_t mode) {
    if (mode == -1 && state >= 0) mode = state;
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
    esp_err_t err = -state;
    switch (err) {
    case ESP_ERR_USBMODE_DISABLED:          return "Disabled";
    case ESP_ERR_USBMODE_NOT_INITED:        return "Uninitialized";
    case ESP_ERR_USBMODE_PENDING_REBOOT:    return "Pending reboot";
    default:                                return esp_err_to_name(err);
    }
}

esp_err_t usbmode_switch(usbmode_t mode, bool reboot) {
    esp_err_t err = ESP_OK;
    if (mode == state) return err;
    bool exited = state == -ESP_ERR_USBMODE_DISABLED || \
                  state == -ESP_ERR_USBMODE_NOT_INITED;
    LOOPN(i, LEN(modes)) {
        if (mode != modes[i].mode) continue;
        LOOPN(j, LEN(modes)) {
            if (state != modes[j].mode || !modes[j].exit) continue;
            if (( err = modes[j].exit(mode) )) {
                ESP_LOGE(TAG, "mode %s exit failed: %s",
                         usbmode_str(state), esp_err_to_name(err));
            } else {
                exited = true;
            }
        }
        config_set("sys.usb.mode", usbmode_str(mode));
        if (!exited) {
            if (reboot) esp_restart();
            state = -ESP_ERR_USBMODE_PENDING_REBOOT;
            ESP_LOGI(TAG, "mode set to %s (pending)", usbmode_str(mode));
            return err;
        }
        if (!( err = modes[i].init ? modes[i].init(state) : ESP_OK )) {
            state = mode;
            ESP_LOGI(TAG, "mode set to %s", usbmode_str(mode));
        } else {
            state = -err;
            ESP_LOGE(TAG, "mode set to %s failed: %s",
                     usbmode_str(mode), esp_err_to_name(err));
        }
        return err;
    }
    ESP_LOGE(TAG, "Invalid mode %d: %s", mode, usbmode_str(mode));
    return ESP_ERR_NOT_FOUND;
}

void usbmode_initialize() {
    LOOPN(i, LEN(modes)) {
        if (strcasecmp(usbmode_str(modes[i].mode), Config.sys.USB_MODE))
            continue;
        usbmode_switch(modes[i].mode, false);
        return;
    }
    if (!strlen(Config.sys.USB_MODE)) {
        ESP_LOGW(TAG, "Software blocked");
        state = -ESP_ERR_USBMODE_DISABLED;
    } else {
        ESP_LOGE(TAG, "Unknown mode. This should not happen!");
    }
}

void usbmode_status() {
    printf("Current mode is %s (%d)\n", usbmode_str(-1), ABS(state));
    if (ISHOST(state)) usbhost_status(state);
    if (ISDEV(state))  usbdev_status(state);
}

#else // CONFIG_BASE_USE_USB

esp_err_t usbmode_switch(usbmode_t m, bool b) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(m); NOTUSED(b);
}

void usbmode_initialize() {}

void usbmode_status() {}

#endif // CONFIG_BASE_USE_USB
