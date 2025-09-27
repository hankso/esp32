/* 
 * File: usbmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#pragma once

#if __has_include("tusb.h")
#   define WITH_TUSB
#   ifndef DUAL_TUSB
#       include "tusb.h"
#   endif
#endif

#include "globals.h"
#include "hidtool.h"

#if defined(CONFIG_BASE_USE_USB) && !defined(SOC_USB_OTG_SUPPORTED)
#   undef CONFIG_BASE_USE_USB
#endif

#ifdef CONFIG_BASE_USB_MSC_DEVICE
#   if !defined(IDF_TARGET_V4) && !__has_include("tusb_msc_storage.h")
#       warning "Run `idf.py add-dependency esp_tinyusb`"
#       undef CONFIG_BASE_USB_MSC_DEVICE
#   endif
#endif

#ifdef CONFIG_BASE_USB_HID_DEVICE
#   if !defined(IDF_TARGET_V4) && !__has_include("class/hid/hid_device.h")
#       warning "Run `idf.py add-dependency esp_tinyusb`"
#       undef CONFIG_BASE_USB_HID_DEVICE
#   endif
#endif

#if defined(CONFIG_BASE_USB_CDC_HOST) && !__has_include("usb/cdc_acm_host.h")
#   warning "Run `idf.py add-dependency usb_host_cdc_acm`"
#   undef CONFIG_BASE_USB_CDC_HOST
#endif

#if defined(CONFIG_BASE_USB_MSC_HOST) && !__has_include("msc_host.h")
#   warning "Run `idf.py add-dependency usb_host_msc`"
#   undef CONFIG_BASE_USB_MSC_HOST
#endif

#if defined(CONFIG_BASE_USB_HID_HOST) && !__has_include("usb/hid_host.h")
#   warning "Run `idf.py add-dependency usb_host_hid`"
#   undef CONFIG_BASE_USB_HID_HOST
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

#define ISDEV(m) ( (m) == CDC_DEVICE || (m) == MSC_DEVICE || (m) == HID_DEVICE )
#define ISHOST(m) ( (m) == CDC_HOST || (m) == MSC_HOST || (m) == HID_HOST )

esp_err_t usbmode_switch(usbmode_t mode, bool reboot_now);

void usbmode_initialize();

void usbmode_status();

#ifdef CONFIG_BASE_USB_HID_DEVICE
bool hidu_send_report(const hid_report_t *);
#endif

#ifdef __cplusplus
}
#endif
