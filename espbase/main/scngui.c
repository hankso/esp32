/*
 * File: scngui.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/3 2:08:42
 */

#include "globals.h"

#include <math.h>

#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER 2.718281828459045235360287471352

#define rad(deg) ( (deg) * DEG_TO_RAD )
#define deg(rad) ( (rad) * RAD_TO_DEG )

#if __has_include("lvgl.h")
#include "lvgl.h"

static const char * TAG = "GUI";

static struct {
    int count_val;
    lv_obj_t *scr;
    lv_obj_t *lbl[2];
    lv_obj_t *img[2];
    lv_obj_t *arc[3];
    lv_timer_t *timer;
} ctx;

extern bool lvgl_ui_progbar(uint8_t pcnt) {
    char buf[64] = { 0 };
    snprintf(buf, sizeof(buf), "%d %%", pcnt);
    lv_label_set_text(ctx.lbl[1], buf);
    return ctx.lbl[1] != NULL;
}

extern void lvgl_ui_label(lv_disp_t *disp) {
    if (ctx.scr) return;
    ctx.scr = lv_disp_get_scr_act(disp);
    const char * str[] = {
        "Hello world! Super looooooooooong string.",
        "TODO: Progressbar not working?"
    };
    LOOPN(i, LEN(ctx.lbl)) {
        ctx.lbl[i] = lv_label_create(ctx.scr);
        lv_label_set_long_mode(ctx.lbl[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(ctx.lbl[i], str[i % LEN(str)]);
        lv_obj_set_width(ctx.lbl[i], disp->driver->hor_res);
        lv_obj_align(ctx.lbl[i], LV_ALIGN_TOP_MID, 0, i * 20);
    }
    ESP_LOGI(TAG, "created %d labels", LEN(ctx.lbl));
}

static lv_obj_t * create_img(lv_obj_t *node, const char *src) {
#ifdef CONFIG_FFS_MP
    lv_obj_t *img = lv_img_create(node);
    char path[3 + strlen(CONFIG_FFS_MP) + strlen(src)];
    snprintf(path, sizeof(path), "S:%s%s", CONFIG_FFS_MP, src);
    ESP_LOGI(TAG, "Create image from %s", path);
    lv_img_set_src(img, path);
    return img;
#else
    return NULL;
#endif
}

static void anim_timer_cb(lv_timer_t *timer) {
    int count = ctx.count_val;
    if (count < 90) {
        lv_coord_t arc_start = count > 0 ? (1 - cosf(rad(count))) * 270 : 0;
        lv_coord_t arc_len = (sinf(rad(count)) + 1) * 135;
        LOOPN(i, LEN(ctx.arc)) {
            lv_arc_set_bg_angles(ctx.arc[i], arc_start, arc_len);
            lv_arc_set_rotation(ctx.arc[i], (count + 120 * (i + 1)) % 360);
        }
    } else if (count == 90) {
        LOOPN(i, LEN(ctx.arc)) { lv_obj_del(ctx.arc[i]); }
        ctx.img[1] = create_img(ctx.scr, "text.png");
        lv_obj_set_style_img_opa(ctx.img[1], 0, 0);
    } else if (100 <= count && count <= 180) {
        lv_coord_t offset = (sinf((count - 140) * 2.25f / 90.0f) + 1) * 20.0f;
        lv_obj_align(ctx.img[0], LV_ALIGN_CENTER, 0, -offset);
        lv_obj_align(ctx.img[1], LV_ALIGN_CENTER, 0, 2 * offset);
        lv_obj_set_style_img_opa(ctx.img[1], offset / 40.0f * 255, 0);
    }
    if (( count += 5 ) == 220) {
        lv_timer_del(timer);
        ctx.timer = NULL;
    } else {
        ctx.count_val = count;
    }
}

extern void lvgl_ui_image(lv_disp_t *disp) {
    if (ctx.timer) return; // already started
    if (!ctx.scr) ctx.scr = lv_disp_get_scr_act(disp);
    if (!ctx.img[0]) ctx.img[0] = create_img(ctx.scr, "logo.png");
    lv_obj_center(ctx.img[0]);
    TRYNULL(ctx.img[1], lv_obj_del);

    lv_color_t color[] = {
        LV_COLOR_MAKE(232, 87, 116),
        LV_COLOR_MAKE(126, 87, 162),
        LV_COLOR_MAKE(90, 202, 228),
    };
    LOOPN(i, LEN(ctx.arc)) {
        if (!ctx.arc[i]) ctx.arc[i] = lv_arc_create(ctx.scr);
        lv_arc_set_value(ctx.arc[i], 0);
        lv_arc_set_bg_angles(ctx.arc[i], 120 * i, 10 + 120 * i);
        lv_obj_set_size(ctx.arc[i], 120 - 30 * i, 120 - 30 * i);
        lv_obj_remove_style(ctx.arc[i], NULL, LV_PART_KNOB);
        lv_obj_set_style_arc_width(ctx.arc[i], 10, 0);
        lv_obj_set_style_arc_color(ctx.arc[i], color[i % LEN(color)], 0);
        lv_obj_center(ctx.arc[i]);
    }

    ctx.count_val = -90;
    ctx.timer = lv_timer_create(anim_timer_cb, 20, NULL);
    ESP_LOGI(TAG, "start animation timer");
}

#endif // has LVGL
