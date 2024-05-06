/*
 * File: screen.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/25 12:28:01
 */

#pragma once

#include "globals.h"

#ifdef CONFIG_BASE_USE_SCREEN
#   if __has_include("esp_lvgl_port.h") && __has_include("lvgl.h")
#       define WITH_LVGL
#   elif __has_include("u8g2.h")
#       define WITH_U8G2
#   else
#       warning "Run `git clone git@github.com:olikraus/u8g2.git`"
#       warning "Run `git clone git@github.com:mkfrey/u8g2-hal-esp-idf.git`"
#       warning "Run `idf.py add-dependency lvgl/lvgl`"
#   endif
#   if !defined(CONFIG_BASE_SCREEN_I2C) && !defined(CONFIG_BASE_SCREEN_SPI)
#       error "Screen interface not defined. Kconfig file error."
#   endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {  // Usage                    Argument
    SCN_INIT,   // initialize
    SCN_EXIT,   // deinitialize
    SCN_STAT,   // print statistics
    SCN_FONT,   // load font from disk      char *
    SCN_INP,    // handle input             hid_report_t *
    SCN_DPI,    // mouse sensitivity        float *         (> 0)
    SCN_ROT,    // change screen rotation   int *           (LV_DISP_ROT_XXX)
    SCN_FPS,    // change refresh rate      int *           (0-100)
    SCN_BTN,    // virtual button click     int *           (0-6)
    SCN_PBAR,   // draw progress bar        int *           (0-100)
} scn_cmd_t;

void screen_initialize();
void screen_status();
esp_err_t screen_command(scn_cmd_t, const void *arg);

#ifdef __cplusplus
}
#endif
