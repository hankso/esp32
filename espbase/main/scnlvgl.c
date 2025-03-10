/*
 * File: scnlvgl.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/3 2:08:42
 * Desc: We are using https://docs.lvgl.io/9.2/
 */

/******************************************************************************
 * Compatibility
 */

#if __has_include("esp_lvgl_port.h")
#   include "screen.h"
#   include "config.h"
#   include "filesys.h"
#   include "hidtool.h"
#   include "esp_lvgl_port.h"
#   include "freertos/FreeRTOS.h"
#   include "freertos/semphr.h"
#   define LOGI(...)            ESP_LOGI("LVGL", __VA_ARGS__)
#   define LOGW(...)            ESP_LOGW("LVGL", __VA_ARGS__)
#   define LOGE(...)            ESP_LOGE("LVGL", __VA_ARGS__)
#   define MUTEX()              xSemaphoreCreateBinary()
#   define ACQUIRE(s)           ( (s) ? xSemaphoreTake((s), TIMEOUT(50)) : 0 )
#   define RELEASE(s)           do { if (s) xSemaphoreGive(s); } while (0)
#   define ERR_NO_ERR           ESP_OK
#   define ERR_NO_MEM           ESP_ERR_NO_MEM
#   define ERR_INVALID_ARG      ESP_ERR_INVALID_ARG
#   define ERR_INVALID_STATE    ESP_ERR_INVALID_STATE
#   define ERR_NOT_FOUND        ESP_ERR_NOT_FOUND
#   define ERR_NOT_SUPPORTED    ESP_ERR_NOT_SUPPORTED
#elif __has_include("lvgl.h") && _WIN32
#   define WITH_LVGL
#   define LOGI(fmt, ...)       fprintf(stderr, fmt "\n", __VA_ARGS__)
#   define LOGW(fmt, ...)       fprintf(stderr, fmt "\n", __VA_ARGS__)
#   define LOGE(fmt, ...)       fprintf(stderr, fmt "\n", __VA_ARGS__)
#   define MUTEX()              CreateMutex(NULL, FALSE, NULL)
#   define ACQUIRE(s)           ( (s) ? !WaitForSingleObject((s), 1) : 0 )
#   define RELEASE(s)           do { if (s) ReleaseMutex(s); } while (0)
enum {
    ERR_NO_ERR,
    ERR_NO_MEM,
    ERR_INVALID_ARG,
    ERR_INVALID_STATE,
    ERR_NOT_FOUND,
    ERR_NOT_SUPPORTED
};
#endif

#ifdef WITH_LVGL
#include "lvgl.h"

/******************************************************************************
 * Context and utilities
 */

#define PI          3.1415926535897932384626433832795
#define HALF_PI     1.5707963267948966192313216916398
#define TWO_PI      6.283185307179586476925286766559
#define DEG_TO_RAD  0.017453292519943295769236907684886
#define RAD_TO_DEG  57.295779513082320876798154814105
#define EULER       2.718281828459045235360287471352

#define RAD(deg) ( (deg) * DEG_TO_RAD )
#define DEG(rad) ( (rad) * RAD_TO_DEG )

#define PREFIX(level)  ( ">-+*"[(level) % 4] ) // see 0x2500-0x257F

#if LVGL_VERSION_MAJOR >= 9
#   define LVGL9
#   define lv_timer_get_period(t)   ( *(uint32_t *)(t) ) // hotfix
#   ifndef lv_disp_get_hor_res
#   define lv_disp_get_hor_res      lv_display_get_horizontal_resolution
#   define lv_disp_get_ver_res      lv_display_get_vertical_resolution
#   endif
#   define send_event               lv_obj_send_event
#   define refresh_timer            lv_display_get_refr_timer
#   define screen_active            lv_display_get_screen_active
#else
#   define LVGL8
#   define lv_timer_get_period(t)   ( (t)->period )
#   define lv_obj_delete            lv_obj_del
#   define lv_obj_remove_flag       lv_obj_clear_flag
#   define lv_text_get_size         lv_txt_get_size
#   define lv_anim_delete           lv_anim_del
#   define lv_group_delete          lv_group_del
#   define lv_image_class           lv_img_class
#   define lv_image_set_src         lv_img_set_src
#   define lv_screen_load           lv_scr_load
#   define lv_screen_load_anim      lv_scr_load_anim
#   define lv_button_class          lv_btn_class
#   define lv_button_create         lv_btn_create
#   define lv_binfont_create        lv_font_load
#   define lv_binfont_destroy       lv_font_free
#   define lv_display_get_theme     lv_disp_get_theme
#   define lv_display_set_theme     lv_disp_set_theme
#   define lv_display_set_default   lv_disp_set_default
#   define lv_display_get_rotation  lv_disp_get_rotation
#   define lv_display_set_rotation  lv_disp_set_rotation
#   define send_event               lv_event_send
#   define refresh_timer            _lv_disp_get_refr_timer
#   define screen_active            lv_disp_get_scr_act
#endif

typedef struct screen screen_t;
typedef int (*screen_cb_t)(screen_t *);
struct screen {
    lv_obj_t *root;
    screen_cb_t init;   // reentrant setup
    screen_cb_t exit;   // cleanup extra resources other than lv_obj_t
    screen_cb_t enter;  // called after entering this screen
    screen_cb_t leave;  // called before leaving this screen
    void *user_data;
    const char *name;
};

static int screen_menu_init(screen_t *);
static int screen_label_init(screen_t *);
static int screen_anim_init(screen_t *);

static const screen_cb_t inits[] = {
    screen_menu_init,
    screen_label_init,
    screen_anim_init,
};

static const lv_indev_type_t types[] = {
    LV_INDEV_TYPE_POINTER,
    LV_INDEV_TYPE_KEYPAD,
    LV_INDEV_TYPE_ENCODER,
};

static struct {
    uint32_t event;                 // custom LVGL event code
    int curr, width, height;        // current screen & display size
    screen_t scr[LEN(inits)];       // created screens
    lv_style_t style_font;          // style for custom font
    lv_theme_t theme_font;          // theme for custom font
    lv_font_t *font;                // pointer to cascaded fonts
    lv_disp_t *disp;                // pointer to current display
    lv_group_t *group;              // input controlled group
    lv_indev_t *indev[LEN(types)];  // input devices
#ifdef LVGL8
    lv_indev_drv_t drv[LEN(types)]; // input device drivers
#endif
    void * mutex;
    struct {
        int x, y;
        float scale;
        bool pressed;
    } pointer;
    struct {
        lv_key_t key;
        bool pressed;
    } keypad;
    struct {
        int diff;
        lv_key_t last;
        bool enter, left, right;
    } encoder;
} ctx;

static int screen_change(screen_t *next, uint32_t anim_ms) {
    int err = ERR_NO_ERR;
    int idx = next - ctx.scr;
    screen_t *prev = ctx.scr + ctx.curr;
    if (!ctx.disp) return ERR_INVALID_STATE;
    if (idx < 0 || idx > LEN(ctx.scr)) return ERR_INVALID_ARG;
    if (!next || !next->root) return ERR_INVALID_ARG;
    if (!prev || !prev->root) return ERR_INVALID_STATE;
    if (next == prev) return err;

    if (prev->leave && ( err = prev->leave(prev) )) return err;
    if (anim_ms) {
        int anim = LV_SCR_LOAD_ANIM_MOVE_LEFT;
        if (next < prev) anim = LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        lv_screen_load_anim(next->root, anim, anim_ms, 0, false);
    } else {
        lv_screen_load(next->root);
    }
    if (next->enter) err = next->enter(next);
    ctx.curr = idx;
    return err;
}

static void dump_obj(lv_obj_t *obj, int lvl) {
    if (!obj) return;
    const char *cstr;
    const lv_obj_class_t *cls = lv_obj_get_class(obj);
         if (cls == &lv_obj_class)      cstr = "screen";
    else if (cls == &lv_arc_class)      cstr = "arc";
    else if (cls == &lv_bar_class)      cstr = "bar";
    else if (cls == &lv_line_class)     cstr = "line";
    else if (cls == &lv_label_class)    cstr = "label";
    else if (cls == &lv_image_class)    cstr = "image";
    else if (cls == &lv_button_class)   cstr = "button";
    else if (cls == &lv_switch_class)   cstr = "switch";
    else if (cls == &lv_slider_class)   cstr = "slider";
    else if (cls == &lv_checkbox_class) cstr = "checkbox";
    else if (cls == &lv_dropdown_class) cstr = "dropdown";
    else                                cstr = "unknown";
    if (lvl >= 0) printf("%*s%c ", 2 * lvl, "", PREFIX(lvl));
    printf("%s %dx%d+%d+%d",
           cstr, lv_obj_get_width(obj), lv_obj_get_height(obj),
           lv_obj_get_x(obj), lv_obj_get_y(obj));
    if (cls == &lv_obj_class) {
        LOOPN(i, LEN(ctx.scr)) {
            if (obj != ctx.scr[i].root) continue;
            printf(" [IDX=%d]", i);
            if (ctx.scr[i].name) {
                printf(" [NAME=%s]", ctx.scr[i].name);
            } else {
                printf(" [PTR=%p]", ctx.scr[i].root);
            }
            if (ctx.curr == i) printf(" [Current]");
        }
    }
    putchar('\n');
    LOOPN(i, lv_obj_get_child_cnt(obj)) {
        dump_obj(lv_obj_get_child(obj, i), lvl >= 0 ? lvl + 1 : lvl);
    }
}

static void dump_font(const lv_font_t *font) {
    for (uint8_t lvl = 0; font; font = font->fallback, lvl++) {
        printf("%*s%c Font ", 2 * lvl, "", PREFIX(lvl));
        if (font->user_data) {
            printf((const char *)font->user_data);
#ifdef CONFIG_LV_FONT_UNSCII_8
        } else if (font == &lv_font_unscii_8) {
            printf("unscii-8");
#endif
        } else if (font == lv_font_default()) {
            printf("default");
        } else {
            printf("%p", font->user_data);
        }
        printf(" [line_height=%d] [base_line=%d] [subpx=%u]\n",
               font->line_height, font->base_line, font->subpx);
    }
}

static lv_obj_t * create_img(const char *fn, lv_obj_t *node) {
#if defined(CONFIG_LV_USE_FS_POSIX) && defined(CONFIG_BASE_USE_FFS)
    char path[2 + PATH_MAX_LEN] = { CONFIG_LV_FS_POSIX_LETTER, ':' };
    snprintf(path + 2, PATH_MAX_LEN, fjoin(2, Config.sys.DIR_DATA, fn));
    lv_obj_t *img = lv_img_create(node ?: lv_disp_get_scr_act(ctx.disp));
    lv_image_set_src(img, path);
#   ifdef LVGL9
    lv_point_t pivot;
    lv_image_get_pivot(img, &pivot);
    LOGI("Load image from %s: %dx%d", path, pivot.x, pivot.y);
#   else
    lv_img_t *ptr = (lv_img_t *)img;
    LOGI("Load image from %s: %dx%d cf %d", path, ptr->w, ptr->h, ptr->cf);
#   endif
    return img;
#else
    return NULL; NOTUSED(fn);
#endif
}

static lv_font_t * create_font(const char *fn) {
#if defined(CONFIG_LV_USE_FS_POSIX) && defined(CONFIG_BASE_USE_FFS)
    char path[2 + PATH_MAX_LEN] = { CONFIG_LV_FS_POSIX_LETTER, ':' };
    snprintf(path + 2, PATH_MAX_LEN, fjoin(2, Config.sys.DIR_DATA, fn));
    if (ctx.font && ctx.font->user_data &&
        !memcmp(path, ctx.font->user_data, strlen(path))) return ctx.font;
    lv_font_t *font = lv_binfont_create(path);
    if (!font || !( font->user_data = strdup(path) )) {
        TRYNULL(font, lv_binfont_destroy);
    } else if (ctx.font) {
        font->fallback = ctx.font;
#   ifdef CONFIG_LV_FONT_UNSCII_8
    } else if (ctx.disp && MIN(ctx.width, ctx.height) < 240) {
        font->fallback = &lv_font_unscii_8;
#   endif
    } else {
        font->fallback = lv_font_default();
    }
    LOGI("Load font from %s: %p", path, font);
    return font;
#else
    return NULL; NOTUSED(fn);
#endif
}

#ifdef LVGL9
static void cb_indev_read(lv_indev_t *indev, lv_indev_data_t *data) {
    lv_indev_type_t type = lv_indev_get_type(indev);
#else
static void cb_indev_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    lv_indev_type_t type = indev_drv->type;
#endif
    if (!ACQUIRE(ctx.mutex)) return;
    if (type == LV_INDEV_TYPE_POINTER) {
        data->point.x = ctx.pointer.x / ctx.pointer.scale;
        data->point.y = ctx.pointer.y / ctx.pointer.scale;
        data->state = ctx.pointer.pressed
            ? LV_INDEV_STATE_PRESSED
            : LV_INDEV_STATE_RELEASED;
    } else if (type == LV_INDEV_TYPE_KEYPAD) {
        data->key = ctx.keypad.key;
        if (ctx.keypad.pressed) {
            data->state = LV_INDEV_STATE_PRESSED;
            ctx.keypad.pressed = false;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
            ctx.keypad.key = 0;
        }
    } else if (type == LV_INDEV_TYPE_ENCODER) {
        // Ignore long press if using buttons with Encoder logic
        if (ctx.encoder.left) {
            data->key = ctx.encoder.last = LV_KEY_LEFT;
            data->state = LV_INDEV_STATE_PRESSED;
            ctx.encoder.left = false;
        } else if (ctx.encoder.right) {
            data->key = ctx.encoder.last = LV_KEY_RIGHT;
            data->state = LV_INDEV_STATE_PRESSED;
            ctx.encoder.right = false;
        } else if (ctx.encoder.enter) {
            data->key = ctx.encoder.last = LV_KEY_ENTER;
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            if (ctx.encoder.last) {
                data->key = ctx.encoder.last;
                ctx.encoder.last = 0;
            }
            data->state = LV_INDEV_STATE_RELEASED;
            data->enc_diff = ctx.encoder.diff;
            ctx.encoder.diff = 0;
        }
    }
    RELEASE(ctx.mutex);
}

/******************************************************************************
 * Screen Menu
 */

static void cb_screen_menu(lv_event_t *e) {
    static uint8_t cnt = 0;
    lv_obj_t *btn = lv_event_get_target(e);
    lv_label_set_text_fmt(lv_obj_get_child(btn, 0), "%d", cnt++);
}

static int screen_menu_init(screen_t *scr) {
    if (!( scr->root = screen_active(ctx.disp) )) return ERR_INVALID_STATE;
    scr->name = "Menu";
    lv_obj_t *lbl = lv_label_create(scr->root);
    lv_label_set_text(lbl, "1.Menu\n2.Nav2D");
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *btn = lv_button_create(scr->root);
    lv_obj_set_size(btn, 40, 20);
    lv_obj_add_event_cb(btn, cb_screen_menu, LV_EVENT_CLICKED, NULL);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *txt = lv_label_create(btn);
    lv_label_set_text(txt, LV_SYMBOL_LEFT "|" LV_SYMBOL_RIGHT);
    lv_obj_center(txt);
    return (lbl && btn && txt) ? ERR_NO_ERR : ERR_NO_MEM;
}

/******************************************************************************
 * Screen Test Label
 */

static void cb_screen_label(lv_event_t *e) {
    static char buf[10];
    lv_obj_t *bar = lv_event_get_target(e);
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc); // label_dsc.font = ctx.font;
    lv_snprintf(buf, sizeof(buf), "%d%%", (int)lv_bar_get_value(bar));

    lv_point_t txt_size;
    lv_text_get_size(
        &txt_size, buf, label_dsc.font, label_dsc.letter_space,
        label_dsc.line_space, LV_COORD_MAX, label_dsc.flag);

    lv_area_t bar_area, txt_area = {
        .x1 = 0, .x2 = txt_size.x - 1, .y1 = 0, .y2 = txt_size.y - 1
    };
    lv_obj_get_coords(bar, &bar_area);
    int width = lv_area_get_width(&bar_area) * lv_bar_get_value(bar) / 100;
    lv_area_set_width(&bar_area, width);
    if (width > txt_size.x + 10) {
        lv_area_align(&bar_area, &txt_area, LV_ALIGN_RIGHT_MID, -5, 0);
        label_dsc.color = lv_color_white();
    } else {
        lv_area_align(&bar_area, &txt_area, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        label_dsc.color = lv_color_black();
    }
# ifdef LVGL9
    label_dsc.text = buf;
    label_dsc.text_local = true;
    lv_draw_label(lv_event_get_layer(e), &label_dsc, &txt_area);
#else
    lv_draw_label(lv_event_get_draw_ctx(e), &label_dsc, &txt_area, buf, NULL);
#endif
}

// Copied from lvgl/src/font/lv_symbol_def.h
// AUDIO, VIDEO, LIST, OK, CLOSE, POWER, SETTINGS, HOME, DOWNLOAD, DRIVE,
// REFERSH, MUTE, VOLUME_MID, VOLUME_MAX, IMAGE, TINT, PREV, PLAY, PAUSE,
// STOP, NEXT, EJECT, LEFT, RIGHT, PLUS, MINUS, EYE_OPEN, EYE_CLOSE, WARNING,
// SHUFFLE, UP, DOWN, LOOP, DIRECTORY, UPLOAD, CALL, CUT, COPY, SAVE, BARS,
// ENVELOP, CHARGE, PASTE, BELL, KEYBOARD, GPS, FILE, WIFI, BATTERY_FULL,
// BATTERY_3, BATTERY_2, BATTERY_1, BATTERY_EMPTY, USB, BLUETOOTH, TRASH,
// EDIT, BACKSPACE, SD_CARD, NEW_LINE
static const uint16_t LV_SYMBOLS[] = {
    61441, 61448, 61451, 61452, 61453, 61457, 61459, 61461, 61465, 61468,
    61473, 61478, 61479, 61480, 61502, 61507, 61512, 61515, 61516, 61517,
    61521, 61522, 61523, 61524, 61543, 61544, 61550, 61552, 61553, 61556,
    61559, 61560, 61561, 61563, 61587, 61589, 61636, 61637, 61639, 61641,
    61664, 61671, 61674, 61683, 61724, 61732, 61787, 61931, 62016, 62017,
    62018, 62019, 62020, 62087, 62099, 62189, 62212, 62810, 63426, 63650
};

static int screen_label_init(screen_t *scr) {
    if (!( scr->root = lv_obj_create(NULL) )) return ERR_NO_MEM;
    scr->name = "Test label";
    lv_obj_t *bar = lv_bar_create(scr->root);
    lv_obj_set_size(bar, ctx.width, 16);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_event_cb(bar, cb_screen_label, LV_EVENT_DRAW_MAIN_END, NULL);
    char buf[LEN(LV_SYMBOLS) * 4], *ptr = buf, *end = buf + sizeof(buf);
    LOOPN(i, LEN(LV_SYMBOLS)) {
        ptr += snprintf(ptr, end - ptr, "%s|", unicode2str(LV_SYMBOLS[i]));
    }
    int lht = 8;
    for (const lv_font_t *font = ctx.font; font; font = font->fallback) {
        lht = MAX(lht, font->line_height);
    }
    lv_obj_t *lbl;
    const char * txts[] = { buf, "中文字体" };
    LOOPN(i, LEN(txts)) {
        if (!( lbl = lv_label_create(scr->root) )) return ERR_NO_MEM;
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(lbl, txts[i]);
        lv_obj_set_width(lbl, ctx.width);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 16 + i * lht);
    }
    return bar ? ERR_NO_ERR : ERR_NO_MEM;
}

/******************************************************************************
 * Screen Test Anim
 */

static void screen_anim_exec(void * var, int32_t val) {
    lv_label_set_text_fmt(lv_obj_get_child(var, -1), "%4d", val);
    lv_coord_t arc_start = val > 0 ? (1 - cosf(RAD(val))) * 270 : 0;
    lv_coord_t arc_len = (sinf(RAD(val)) + 1) * 135;
    LOOPN(i, lv_obj_get_child_cnt(var) - 2) {
        lv_obj_t *arc = lv_obj_get_child(var, i);
        lv_arc_set_bg_angles(arc, arc_start, arc_len);
        lv_arc_set_rotation(arc, (val + 120 * i) % 360);
    }
}

static void cb_screen_anim(lv_event_t *e) {
    lv_obj_t *sw = lv_event_get_target(e);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, lv_obj_get_parent(sw));
        lv_anim_set_time(&a, lv_anim_speed_to_time(45, -90, 90));
        lv_anim_set_values(&a, -90, 90);
        lv_anim_set_exec_cb(&a, screen_anim_exec);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    } else {
        lv_anim_delete(lv_obj_get_parent(sw), screen_anim_exec);
    }
}

static int screen_anim_init(screen_t *scr) {
    if (!( scr->root = lv_obj_create(NULL) )) return ERR_NO_MEM;
    scr->name = "Test Anim";

    lv_obj_t *arc;
    int dia = MIN(ctx.width, ctx.height);
#ifndef CONFIG_LV_COLOR_DEPTH_1
    const lv_color_t color[] = {
        LV_COLOR_MAKE(232, 87, 116),
        LV_COLOR_MAKE(126, 87, 162),
        LV_COLOR_MAKE(90, 202, 228),
    };
#endif
    LOOPN(i, dia / 26) {
        if (!( arc = lv_arc_create(scr->root) )) return ERR_NO_MEM;
        lv_arc_set_value(arc, 0);
        lv_arc_set_bg_angles(arc, 120 * i, 10 + 120 * i);
        lv_obj_set_size(arc, dia - 26 * i, dia - 26 * i);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc, 3, 0);
        lv_obj_set_style_border_width(arc, 0, 0);
#ifdef CONFIG_LV_COLOR_DEPTH_1
        lv_obj_set_style_arc_color(arc, lv_color_black(), 0);
#else
        lv_obj_set_style_arc_color(arc, color[i % LEN(color)], 0);
#endif
        lv_obj_center(arc);
    }

    lv_obj_t *sw = lv_switch_create(scr->root);
    lv_obj_add_event_cb(sw, cb_screen_anim, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(sw, 40, 20);
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *lbl = lv_label_create(scr->root);
    return (sw && lbl) ? ERR_NO_ERR : ERR_NO_MEM;
}

/******************************************************************************
 * UI Entry point and event handler
 */

static int lvgl_ui_input(hid_report_t *rpt) {
    if (rpt->id == REPORT_ID_KEYBD) {
        hid_handle_keybd(HID_TARGET_SCN, &rpt->keybd, NULL);
        bool shift = HAS_SHIFT(rpt->keybd.modifier);
        ITER(key, rpt->keybd.keycode) {
            if (key <= HID_KEY_ERROR_UNDEFINED || !ACQUIRE(ctx.mutex)) continue;
            const char *str = keycode2str(key, rpt->keybd.modifier);
            switch (key) {
            case HID_KEY_ARROW_UP:    ctx.keypad.key = LV_KEY_UP;   break;
            case HID_KEY_ARROW_DOWN:  ctx.keypad.key = LV_KEY_DOWN; break;
            case HID_KEY_ARROW_RIGHT: ctx.keypad.key = LV_KEY_RIGHT; break;
            case HID_KEY_ARROW_LEFT:  ctx.keypad.key = LV_KEY_LEFT; break;
            case HID_KEY_ESCAPE:      ctx.keypad.key = LV_KEY_ESC;  break;
            case HID_KEY_DELETE:      ctx.keypad.key = LV_KEY_DEL;  break;
            case HID_KEY_BACKSPACE:   ctx.keypad.key = LV_KEY_BACKSPACE; break;
            case HID_KEY_HOME:        ctx.keypad.key = LV_KEY_HOME; break;
            case HID_KEY_END:         ctx.keypad.key = LV_KEY_END;  break;
            case HID_KEY_TAB:   //       '\t'          '\x0B'
                ctx.keypad.key = shift ? LV_KEY_PREV : LV_KEY_NEXT; break;
            case HID_KEY_ENTER: //       '\n'
                ctx.keypad.key = shift ? LV_KEY_ENTER : '\r';       break;
            default:
                ctx.keypad.key = strlen(str) == 1 ? str[0] : 0;     break;
            }
            ctx.keypad.pressed = ctx.keypad.key != 0;
            RELEASE(ctx.mutex);
        }
    } else if (rpt->id == REPORT_ID_MOUSE && ACQUIRE(ctx.mutex)) {
        hid_handle_mouse(HID_TARGET_SCN, &rpt->mouse, NULL, NULL);
        ctx.pointer.x = MAX(0, ctx.pointer.x + rpt->mouse.x);
        ctx.pointer.y = MAX(0, ctx.pointer.y + rpt->mouse.y);
        ctx.pointer.x = MIN(ctx.pointer.x, ctx.width * ctx.pointer.scale);
        ctx.pointer.y = MIN(ctx.pointer.y, ctx.height * ctx.pointer.scale);
        ctx.pointer.pressed = rpt->mouse.buttons & MOUSE_BUTTON_LEFT;
        if (rpt->mouse.buttons & MOUSE_BUTTON_RIGHT) {
            ctx.keypad.key = LV_KEY_ESC;
            ctx.keypad.pressed = true;
        }
        RELEASE(ctx.mutex);
    } else if (rpt->id == REPORT_ID_DIAL && ACQUIRE(ctx.mutex)) {
        switch (rpt->dial[0]) {
        case DIAL_L:    ctx.encoder.left = true; break;
        case DIAL_R:    ctx.encoder.right = true; break;
        case DIAL_DN:   ctx.encoder.enter = true; break;
        case DIAL_UP:   ctx.encoder.enter = false; break;
        }
        RELEASE(ctx.mutex);
    } else {
        return ERR_NOT_SUPPORTED;
    }
    return ERR_NO_ERR;
}

static int lvgl_ui_exit() {
    screen_change(ctx.scr + 0, 0); // return to first screen
    LOOPN(i, LEN(ctx.indev)) {
        TRYNULL(ctx.indev[i], lv_indev_delete);
    }
    LOOPN(i, LEN(ctx.scr)) {
        if (ctx.scr[i].exit) ctx.scr[i].exit(ctx.scr + i);
        if (i) TRYNULL(ctx.scr[i].root, lv_obj_delete);
    }
    for (const lv_font_t *font = ctx.font; font; font = font->fallback) {
        if (font->user_data) {                  // loaded by create_font
            free(font->user_data);              // strdup
            lv_binfont_destroy((lv_font_t *)font);
        } else {
            ctx.font = (lv_font_t *)font;
        }
    }
    TRYNULL(ctx.group, lv_group_delete);
    ctx.disp = NULL;
    return ERR_NO_ERR;
}

static void cb_theme_apply(lv_theme_t *theme, lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_label_class))
        lv_obj_add_style(obj, &ctx.style_font, 0);
    LV_UNUSED(theme);
}

static void cb_screen_event(lv_event_t *e) {
    if (lv_event_get_code(e) != ctx.event) return;
    int *ptr = lv_event_get_param(e);
    if (!ptr) return;
    lv_obj_t *obj = lv_event_get_current_target(e);
    LOGI("%p got button %d\n", obj, *ptr);
    if (0 <= *ptr && *ptr < LEN(ctx.scr)) {
        screen_change(ctx.scr + *ptr, 300);
    } else if (ctx.curr == 0) {
        send_event(lv_obj_get_child(ctx.scr[0].root, 1), LV_EVENT_CLICKED, NULL);
    }
}

static int lvgl_ui_init(lv_display_t *disp) {
    int err = ERR_NO_ERR;
    if (!disp || ctx.disp) return err;
    if (!ctx.mutex && ( ctx.mutex = MUTEX() )) RELEASE(ctx.mutex);
    if (!ctx.event) ctx.event = lv_event_register_id();
    if (!ctx.pointer.scale) ctx.pointer.scale = 5; // default 5x

    // 1. Initialize display, input group and screen info
    ctx.curr = 0;
    ctx.disp = disp;
    ctx.group = lv_group_create();
    ctx.width = lv_disp_get_hor_res(disp);
    ctx.height = lv_disp_get_ver_res(disp);
    lv_group_set_default(ctx.group);
    lv_display_set_default(ctx.disp);

    // 2. Register input devices
    LOOPN(i, LEN(types)) {
#ifdef LVGL9
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, types[i]);
        lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
        lv_indev_set_display(indev, disp);
        lv_indev_set_read_cb(indev, cb_indev_read);
#else
        lv_indev_drv_init(ctx.drv + i);
        ctx.drv[i].disp = disp;
        ctx.drv[i].type = types[i];
        ctx.drv[i].read_cb = cb_indev_read;
        lv_indev_t *indev = lv_indev_drv_register(ctx.drv + i);
#endif
        if (!indev) continue;
        if (types[i] == LV_INDEV_TYPE_POINTER)
            lv_indev_set_cursor(indev, create_img("cursor.png", NULL));
        if (types[i] == LV_INDEV_TYPE_ENCODER)
            lv_indev_set_group(indev, ctx.group);
        ctx.indev[i] = indev;
    }

    // 3. Load custom font and configure theme and style
    if (!( ctx.font = create_font("lv_font_chinese_12.bin") ))
        ctx.font = (lv_font_t *)lv_font_default();
    lv_theme_t *theme = lv_display_get_theme(disp);
#if defined(CONFIG_LV_USE_THEME_DEFAULT)
    if (!lv_theme_default_is_inited()) {
        lv_color_t p = lv_color_black(), s = lv_color_white();
        theme = lv_theme_default_init(ctx.disp, p, s, false, ctx.font);
    } else {
        theme = lv_theme_default_get();
    }
#elif defined(CONFIG_LV_USE_THEME_MONO)
    theme = lv_theme_mono_init(disp, false, ctx.font);
#endif
    if (theme) {
        lv_style_init(&ctx.style_font);
        lv_style_set_text_font(&ctx.style_font, ctx.font);
        ctx.theme_font = *theme;
        lv_theme_set_parent(&ctx.theme_font, theme);
        lv_theme_set_apply_cb(&ctx.theme_font, cb_theme_apply);
        lv_display_set_theme(disp, &ctx.theme_font);
    }

    // 4. Initialize each screen
    LOOPN(i, LEN(ctx.scr)) {
        ctx.scr[i].init = inits[i];
        if (ctx.scr[i].root) continue;
        if (!( err = ctx.scr[i].init(ctx.scr + i) )) {
            lv_obj_add_event_cb(
                ctx.scr[i].root, cb_screen_event, ctx.event, NULL);
        } else if (err == ERR_NO_MEM) {
            TRYNULL(ctx.scr[i].root, lv_obj_delete);
        }
    }
    return ERR_NO_ERR;
}

extern int lvgl_ui_cmd(scn_cmd_t cmd, const void *data) {
    if (cmd == SCN_INIT) return lvgl_ui_init((lv_display_t *)data);
    if (cmd == SCN_EXIT) return lvgl_ui_exit();
    if (!ctx.disp) {
        LOGE("not initialized yet");
        return ERR_INVALID_STATE;
    }
    switch (cmd) {
    case SCN_INP: return lvgl_ui_input((hid_report_t *)data);
    case SCN_STAT: {
        uint32_t period = lv_timer_get_period(lv_anim_get_timer());
        printf("LVGL: %d anim @ %d FPS\n",
               lv_anim_count_running(), period ? 1000 / period : 0);
        dump_font(ctx.font);
        LOOPN(i, LEN(ctx.scr)) {
            if (!ctx.scr[i].root) {
                printf("screen %d not initialized\n", i);
            } else {
                dump_obj(ctx.scr[i].root, 0);
            }
        }
    }   break;
    case SCN_FONT: {
        if (!data || !strlen((const char *)data)) return ERR_INVALID_ARG;
        if (!lv_display_get_theme(ctx.disp)) return ERR_INVALID_STATE;
        lv_font_t *font = create_font(data);
        if (!font) return ERR_NOT_FOUND;
        lv_style_reset(&ctx.style_font);
        lv_style_set_text_font(&ctx.style_font, ctx.font = font);
    }   break;
    case SCN_DPI:
        if (!data || *(float *)data <= 0) return ERR_INVALID_ARG;
        ctx.pointer.scale = *(float *)data;
        break;
    case SCN_ROT: {
        lv_display_rotation_t rot = lv_display_get_rotation(ctx.disp);
        rot = data ? *(int *)data : (rot + 1);
        if (rot > LV_DISPLAY_ROTATION_270) rot = LV_DISPLAY_ROTATION_0;
        lv_display_set_rotation(ctx.disp, rot);
        printf("Set rotation to %d\n", rot);
    }   break;
    case SCN_FPS: {
        static uint32_t backup[3];
        int fps = CONS(*(int *)data, 0, 100);
        lv_timer_t *timer[2] = { lv_anim_get_timer(), refresh_timer(ctx.disp) };
        LOOPN(i, LEN(timer)) {
            if (!timer[i]) continue;
            if (fps) {
                if (!backup[2]) backup[i] = lv_timer_get_period(timer[i]);
                lv_timer_set_period(timer[i], 1000 / fps);
                LOGI("set timer#%d period to %d", i, 1000 / fps);
            } else if (backup[2]) {
                LOGI("set timer#%d period to %d", i, backup[i]);
                lv_timer_set_period(timer[i], backup[i]);
            }
        }
        backup[2] = fps > 0;
    }   break;
    case SCN_BTN:
        send_event(screen_active(ctx.disp), ctx.event, (void *)data);
        break;
    case SCN_PBAR: {
        if (!ctx.scr[1].root) return ERR_INVALID_STATE; // on the second screen
        uint8_t val = MIN(*(uint8_t *)data, 100);
        lv_bar_set_value(lv_obj_get_child(ctx.scr[1].root, 0), val, LV_ANIM_ON);
        printf("Set progressbar to %d\n", val);
    }   break;
    default: return ERR_INVALID_ARG;
    }
    return ERR_NO_ERR;
}
#endif // WITH_LVGL
