/* 
 * File: usbmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#pragma once

#include "globals.h"

#ifdef CONFIG_USE_USB
#   include "soc/soc_caps.h"
#   if !SOC_USB_OTG_SUPPORTED
#       undef CONFIG_USE_USB
#   endif
#endif

#ifdef CONFIG_USB_MSC_DEVICE
#   if !defined(CONFIG_FFS_FAT) && !defined(CONFIG_USE_SDFS)
#       warning "Internal Flash and SDCard storage are not supported"
#       undef CONFIG_USB_MSC_DEVICE
#   endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CDC_HOST,
    CDC_DEVICE,
    MSC_HOST,
    MSC_DEVICE,
    HID_HOST,
    HID_DEVICE,
    SERIAL_JTAG,
} usbmode_t;

esp_err_t usbmode_switch(usbmode_t mode, bool reboot_now);

void usbmode_initialize();

void usbmode_status();

// private functions implemented in usbmodeh.c
void usbmodeh_status(usbmode_t mode);
esp_err_t cdc_host_init();
esp_err_t cdc_host_exit();
esp_err_t msc_host_init();
esp_err_t msc_host_exit();
esp_err_t hid_host_init();
esp_err_t hid_host_exit();

// private functions implemented in usbmoded.c
bool usbmoded_device(const void *desc);
void usbmoded_status(usbmode_t mode);
esp_err_t cdc_device_init();
esp_err_t cdc_device_exit();
esp_err_t msc_device_init();
esp_err_t msc_device_exit();
esp_err_t hid_device_init();
esp_err_t hid_device_exit();

// public utilities related to HID device mode
typedef enum {
    DIAL_UP = 0x00, // button release
    DIAL_DN = 0x01, // button press
    DIAL_L  = 0x38, // knob rotate ccw
    DIAL_R  = 0xC8, // knob rotate cw
    DIAL_LF = 0xEC,
    DIAL_RF = 0x14,
} hid_dial_keycode_t;

bool hid_report_dial(hid_dial_keycode_t);
bool hid_report_dial_button(uint32_t ms);

bool hid_report_mouse(uint8_t b, int8_t x, int8_t y, int8_t v, int8_t h);
bool hid_report_mouse_click(const char *str, uint32_t ms);
#define hid_report_mouse_move(x, y)    hid_report_mouse(0, (x), (y), 0, 0)
#define hid_report_mouse_scroll(v, h)  hid_report_mouse(0, 0, 0, (v), (h))
#define hid_report_mouse_button(btn)   hid_report_mouse((btn), 0, 0, 0, 0)

bool hid_report_keyboard(uint8_t modifier, const uint8_t *keycode, size_t len);
bool hid_report_keyboard_press(const char *str, uint32_t ms);

#ifdef __cplusplus
}
#endif
