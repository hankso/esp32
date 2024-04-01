/* 
 * File: usbmode.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#include "usbmode.h"
#include "config.h"

#ifdef CONFIG_USE_USB

#if SOC_USB_SERIAL_JTAG_SUPPORTED
#   include "driver/usb_serial_jtag.h"
#endif

#define ESP_ERR_DISABLED        0x501
#define ESP_ERR_NOT_INITED      0x502
#define ESP_ERR_PENDING_REBOOT  0x503

static const char *TAG = "USBMode";
static int state = -ESP_ERR_NOT_INITED;

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
 * USBMode APIs
 */

static struct {
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
        CASESTR(ESP_ERR_DISABLED, 8);
        CASESTR(ESP_ERR_NOT_INITED, 8);
        CASESTR(ESP_ERR_PENDING_REBOOT, 8);
        default: return esp_err_to_name(err);
    }
}

esp_err_t usbmode_switch(usbmode_t mode, bool restart) {
    esp_err_t err = ESP_OK;
    if (mode == state) return err;
    bool exited = state == -ESP_ERR_DISABLED || \
                  state == -ESP_ERR_NOT_INITED;
    LOOPN(i, LEN(modes)) {
        if (mode != modes[i].mode) continue;
        LOOPN(j, LEN(modes)) {
            if (state != modes[j].mode || !modes[j].exit) continue;
            if (( err = modes[j].exit(mode) )) {
                ESP_LOGE(TAG, "USB mode %s exit failed: %s",
                        usbmode_str(state), esp_err_to_name(err));
            } else {
                exited = true;
            }
        }
        config_set("app.usb.mode", usbmode_str(mode));
        if (!exited) {
            if (restart) esp_restart(); // reboot here
            state = -ESP_ERR_PENDING_REBOOT;
            ESP_LOGI(TAG, "USB mode set to %s (pending)", usbmode_str(mode));
            return err;
        }
        if (!( err = modes[i].init ? modes[i].init(state) : ESP_OK )) {
            state = mode;
            ESP_LOGI(TAG, "USB mode set to %s", usbmode_str(mode));
        } else {
            ESP_LOGE(TAG, "USB mode set to %s failed: %s",
                    Config.app.USB_MODE, esp_err_to_name(err));
            state = -err;
        }
        return err;
    }
    ESP_LOGE(TAG, "Invalid USB mode %d: %s", mode, usbmode_str(mode));
    return ESP_ERR_NOT_FOUND;
}

void usbmode_initialize() {
    LOOPN(i, LEN(modes)) {
        if (strcmp(usbmode_str(modes[i].mode), Config.app.USB_MODE)) continue;
        usbmode_switch(modes[i].mode, false);
        return;
    }
    if (!strlen(Config.app.USB_MODE)) {
        ESP_LOGW(TAG, "USB is software blocked");
        state = -ESP_ERR_DISABLED;
    } else {
        ESP_LOGE(TAG, "Unknown USB mode. This should not happen!");
    }
}

void usbmode_status() {
    printf("USB mode is %s (%d)\n", usbmode_str(-1), ABS(state));
    if (ISHOST(state)) usbmodeh_status(state);
    if (ISDEV(state))  usbmoded_status(state);
}

#else // CONFIG_USE_USB

esp_err_t usbmode_switch(usbmode_t m, bool b) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(m); NOTUSED(b);
}

void usbmode_initialize() {}

void usbmode_status() {}

#endif // CONFIG_USE_USB
