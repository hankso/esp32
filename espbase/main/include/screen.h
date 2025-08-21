/*
 * File: screen.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/25 12:28:01
 */

#pragma once

#include "globals.h"

#ifdef CONFIG_BASE_USE_SCN
#   if __has_include("esp_lvgl_port.h") && __has_include("lvgl.h")
#       define WITH_LVGL
#   elif __has_include("u8g2.h")
#       define WITH_U8G2
#   else
#       warning "Run `git clone git@github.com:olikraus/u8g2.git`"
#       warning "Run `git clone git@github.com:mkfrey/u8g2-hal-esp-idf.git`"
#       warning "Run `idf.py add-dependency lvgl/lvgl`"
#   endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {  // Usage                    Argument
    SCN_INIT,   // initialize               lv_disp_t *
    SCN_EXIT,   // deinitialize
    SCN_STAT,   // print statistics
    SCN_INP,    // handle input             hid_report_t *
    SCN_FONT,   // load font from disk      char *  (under DIR_DATA)
    SCN_DPI,    // mouse sensitivity        float * (> 0)
    SCN_ROT,    // change screen rotation   int *   (LV_DISPLAY_ROTATION_XXX)
    SCN_GAP,    // change screen gap pixel  int *   (x = HIGH8, y = LOW8)
    SCN_FPS,    // change refresh rate      int *   (0-100)
    SCN_BTN,    // virtual button click     int *   (0-6)
    SCN_PBAR,   // draw progress bar        int *   (0-100)
} scn_cmd_t;

void scn_initialize();
void scn_status();
esp_err_t scn_command(scn_cmd_t, const void *arg);

#ifdef __cplusplus
}
#endif
