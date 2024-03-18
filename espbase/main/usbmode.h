/* 
 * File: usbmode.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2024-03-15 00:02:25
 */

#pragma once

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SERIAL_JTAG,
    CDC_DEVICE,
    CDC_HOST,
    MSC_DEVICE,
    MSC_HOST,
    HID_DEVICE,
    HID_HOST,
} usbmode_t;

esp_err_t usbmode_switch(usbmode_t mode, bool reboot_now);

void usbmode_initialize();

void usbmode_status();

#ifdef __cplusplus
}
#endif
