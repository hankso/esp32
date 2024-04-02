/*
 * File: scngui.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/3 2:08:42
 */

#if __has_include("lvgl.h")
#include "lvgl.h"

extern void lvgl_ui(lv_disp_t *disp) {
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, "Hello world! Super looooooooooong string.");
    lv_obj_set_width(label, disp->driver->hor_res);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
}
#endif
