/* 
 * File: usbmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#pragma once

#include "globals.h"
#include "hidtool.h"

#if defined(CONFIG_BASE_USE_USB) && !defined(SOC_USB_OTG_SUPPORTED)
#   undef CONFIG_BASE_USE_USB
#endif

#ifndef IDF_TARGET_V4
#   if CONFIG_BASE_USB_CDC_DEVICE && !__has_include("tusb_cdc_acm.h")
#       warning "Run `idf.py add-dependency espressif/esp_tinyusb`"
#       undef CONFIG_BASE_USB_CDC_DEVICE
#   endif
#   if CONFIG_BASE_USB_MSC_DEVICE && !__has_include("tusb_msc_storage.h")
#       warning "Run `idf.py add-dependency espressif/esp_tinyusb`"
#       undef CONFIG_BASE_USB_MSC_DEVICE
#   endif
#   if CONFIG_BASE_USB_NCM_DEVICE && !__has_include("tinyusb_net.h")
#       warning "Run `idf.py add-dependency espressif/esp_tinyusb`"
#       undef CONFIG_BASE_USB_NCM_DEVICE
#   endif
#   if CONFIG_BASE_USB_HID_DEVICE && !__has_include("class/hid/hid_device.h")
#       warning "Run `idf.py add-dependency espressif/tinyusb`"
#       undef CONFIG_BASE_USB_HID_DEVICE
#   endif
#elif CONFIG_BASE_USB_NCM_DEVICE
#   warning "IDF-v4 does not support usb ncm device"
#   undef CONFIG_BASE_USB_NCM_DEVICE
#endif

#if defined(CONFIG_BASE_USB_CDC_HOST) && !__has_include("usb/cdc_acm_host.h")
#   warning "Run `idf.py add-dependency espressif/usb_host_cdc_acm`"
#   undef CONFIG_BASE_USB_CDC_HOST
#endif

#if defined(CONFIG_BASE_USB_MSC_HOST) && !__has_include("msc_host.h")
#   warning "Run `idf.py add-dependency espressif/usb_host_msc`"
#   undef CONFIG_BASE_USB_MSC_HOST
#endif

#if defined(CONFIG_BASE_USB_HID_HOST) && !__has_include("usb/hid_host.h")
#   warning "Run `idf.py add-dependency espressif/usb_host_hid`"
#   undef CONFIG_BASE_USB_HID_HOST
#endif

#if defined(CONFIG_BASE_USB) && !__has_include("usb/usb_host.h")
#   warning "Run `idf.py add-dependency espressif/usb`"
#   undef CONFIG_BASE_USB
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERIAL_JTAG,
    CDC_HOST,
    MSC_HOST,
    HID_HOST,
    CDC_DEV,
    MSC_DEV,
    HID_DEV,
    NCM_DEV,
    USB_MODE_MAX
} usbmode_t;

#define ISHOST(m)   ( (m) == CDC_HOST || (m) == MSC_HOST || (m) == HID_HOST )
#define ISDEV(m)    ( !ISHOST(m) && (m) != SERIAL_JTAG )

esp_err_t usbmode_switch(usbmode_t mode, bool reboot_now);

void usbmode_initialize();

void usbmode_status();

#ifdef CONFIG_BASE_USB_HID_DEVICE
bool hidu_send_report(const hid_report_t *);
#endif

#ifdef __cplusplus
}
#endif
