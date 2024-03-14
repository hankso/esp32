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
    CDC_DEVICE,
    HID_DEVICE,
    CDC_HOST,
    MSC_HOST,
} usbmode_t;

const char * usbmode_str(usbmode_t mode);

esp_err_t usbmode_switch(usbmode_t mode, bool reboot_now);

void usbmode_initialize();

#ifdef __cplusplus
}
#endif
