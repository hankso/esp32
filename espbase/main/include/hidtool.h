/*
 * File: hidtool.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/17 12:53:12
 */

#pragma once

#include "globals.h"
#include "esp_bit_defs.h"

#ifndef HID_KEY_NONE
#   define HID_KEY_NONE                     0x00
#   define HID_KEY_OVF                      0x01
#   define HID_KEY_FAIL                     0x02
#   define HID_KEY_A                        0x04
#   define HID_KEY_B                        0x05
#   define HID_KEY_C                        0x06
#   define HID_KEY_D                        0x07
#   define HID_KEY_E                        0x08
#   define HID_KEY_F                        0x09
#   define HID_KEY_G                        0x0A
#   define HID_KEY_H                        0x0B
#   define HID_KEY_I                        0x0C
#   define HID_KEY_J                        0x0D
#   define HID_KEY_K                        0x0E
#   define HID_KEY_L                        0x0F
#   define HID_KEY_M                        0x10
#   define HID_KEY_N                        0x11
#   define HID_KEY_O                        0x12
#   define HID_KEY_P                        0x13
#   define HID_KEY_Q                        0x14
#   define HID_KEY_R                        0x15
#   define HID_KEY_S                        0x16
#   define HID_KEY_T                        0x17
#   define HID_KEY_U                        0x18
#   define HID_KEY_V                        0x19
#   define HID_KEY_W                        0x1A
#   define HID_KEY_X                        0x1B
#   define HID_KEY_Y                        0x1C
#   define HID_KEY_Z                        0x1D
#   define HID_KEY_1                        0x1E
#   define HID_KEY_2                        0x1F
#   define HID_KEY_3                        0x20
#   define HID_KEY_4                        0x21
#   define HID_KEY_5                        0x22
#   define HID_KEY_6                        0x23
#   define HID_KEY_7                        0x24
#   define HID_KEY_8                        0x25
#   define HID_KEY_9                        0x26
#   define HID_KEY_0                        0x27
#   define HID_KEY_ENTER                    0x28
#   define HID_KEY_ESCAPE                   0x29
#   define HID_KEY_BACKSPACE                0x2A
#   define HID_KEY_TAB                      0x2B
#   define HID_KEY_SPACE                    0x2C
#   define HID_KEY_MINUS                    0x2D
#   define HID_KEY_EQUAL                    0x2E
#   define HID_KEY_BRACKET_LEFT             0x2F
#   define HID_KEY_BRACKET_RIGHT            0x30
#   define HID_KEY_BACKSLASH                0x31
#   define HID_KEY_EUROPE_1                 0x32
#   define HID_KEY_SEMICOLON                0x33
#   define HID_KEY_APOSTROPHE               0x34
#   define HID_KEY_GRAVE                    0x35
#   define HID_KEY_COMMA                    0x36
#   define HID_KEY_PERIOD                   0x37
#   define HID_KEY_SLASH                    0x38
#   define HID_KEY_CAPS_LOCK                0x39
#   define HID_KEY_F1                       0x3A
#   define HID_KEY_F2                       0x3B
#   define HID_KEY_F3                       0x3C
#   define HID_KEY_F4                       0x3D
#   define HID_KEY_F5                       0x3E
#   define HID_KEY_F6                       0x3F
#   define HID_KEY_F7                       0x40
#   define HID_KEY_F8                       0x41
#   define HID_KEY_F9                       0x42
#   define HID_KEY_F10                      0x43
#   define HID_KEY_F11                      0x44
#   define HID_KEY_F12                      0x45
#   define HID_KEY_PRINT_SCREEN             0x46
#   define HID_KEY_SCROLL_LOCK              0x47
#   define HID_KEY_PAUSE                    0x48
#   define HID_KEY_INSERT                   0x49
#   define HID_KEY_HOME                     0x4A
#   define HID_KEY_PAGE_UP                  0x4B
#   define HID_KEY_DELETE                   0x4C
#   define HID_KEY_END                      0x4D
#   define HID_KEY_PAGE_DOWN                0x4E
#   define HID_KEY_ARROW_RIGHT              0x4F
#   define HID_KEY_ARROW_LEFT               0x50
#   define HID_KEY_ARROW_DOWN               0x51
#   define HID_KEY_ARROW_UP                 0x52
#   define HID_KEY_NUM_LOCK                 0x53
#   define HID_KEY_KEYPAD_DIVIDE            0x54
#   define HID_KEY_KEYPAD_MULTIPLY          0x55
#   define HID_KEY_KEYPAD_SUBTRACT          0x56
#   define HID_KEY_KEYPAD_ADD               0x57
#   define HID_KEY_KEYPAD_ENTER             0x58
#   define HID_KEY_KEYPAD_1                 0x59
#   define HID_KEY_KEYPAD_2                 0x5A
#   define HID_KEY_KEYPAD_3                 0x5B
#   define HID_KEY_KEYPAD_4                 0x5C
#   define HID_KEY_KEYPAD_5                 0x5D
#   define HID_KEY_KEYPAD_6                 0x5E
#   define HID_KEY_KEYPAD_7                 0x5F
#   define HID_KEY_KEYPAD_8                 0x60
#   define HID_KEY_KEYPAD_9                 0x61
#   define HID_KEY_KEYPAD_0                 0x62
#   define HID_KEY_KEYPAD_DECIMAL           0x63
#   define HID_KEY_EUROPE_2                 0x64
#   define HID_KEY_APPLICATION              0x65
#   define HID_KEY_POWER                    0x66
#   define HID_KEY_KEYPAD_EQUAL             0x67
#   define HID_KEY_F13                      0x68
#   define HID_KEY_F14                      0x69
#   define HID_KEY_F15                      0x6A
#   define HID_KEY_F16                      0x6B
#   define HID_KEY_F17                      0x6C
#   define HID_KEY_F18                      0x6D
#   define HID_KEY_F19                      0x6E
#   define HID_KEY_F20                      0x6F
#   define HID_KEY_F21                      0x70
#   define HID_KEY_F22                      0x71
#   define HID_KEY_F23                      0x72
#   define HID_KEY_F24                      0x73
#   define HID_KEY_EXECUTE                  0x74
#   define HID_KEY_HELP                     0x75
#   define HID_KEY_MENU                     0x76
#   define HID_KEY_SELECT                   0x77
#   define HID_KEY_STOP                     0x78
#   define HID_KEY_AGAIN                    0x79
#   define HID_KEY_UNDO                     0x7A
#   define HID_KEY_CUT                      0x7B
#   define HID_KEY_COPY                     0x7C
#   define HID_KEY_PASTE                    0x7D
#   define HID_KEY_FIND                     0x7E
#   define HID_KEY_MUTE                     0x7F
#   define HID_KEY_VOLUME_UP                0x80
#   define HID_KEY_VOLUME_DOWN              0x81
#   define HID_KEY_LOCKING_CAPS_LOCK        0x82
#   define HID_KEY_LOCKING_NUM_LOCK         0x83
#   define HID_KEY_LOCKING_SCROLL_LOCK      0x84
#   define HID_KEY_KEYPAD_COMMA             0x85
#   define HID_KEY_KEYPAD_EQUAL_SIGN        0x86
#   define HID_KEY_KANJI1                   0x87
#   define HID_KEY_KANJI2                   0x88
#   define HID_KEY_KANJI3                   0x89
#   define HID_KEY_KANJI4                   0x8A
#   define HID_KEY_KANJI5                   0x8B
#   define HID_KEY_KANJI6                   0x8C
#   define HID_KEY_KANJI7                   0x8D
#   define HID_KEY_KANJI8                   0x8E
#   define HID_KEY_KANJI9                   0x8F
#   define HID_KEY_LANG1                    0x90
#   define HID_KEY_LANG2                    0x91
#   define HID_KEY_LANG3                    0x92
#   define HID_KEY_LANG4                    0x93
#   define HID_KEY_LANG5                    0x94
#   define HID_KEY_LANG6                    0x95
#   define HID_KEY_LANG7                    0x96
#   define HID_KEY_LANG8                    0x97
#   define HID_KEY_LANG9                    0x98
#   define HID_KEY_ALTERNATE_ERASE          0x99
#   define HID_KEY_SYSREQ_ATTENTION         0x9A
#   define HID_KEY_CANCEL                   0x9B
#   define HID_KEY_CLEAR                    0x9C
#   define HID_KEY_PRIOR                    0x9D
#   define HID_KEY_RETURN                   0x9E
#   define HID_KEY_SEPARATOR                0x9F
#   define HID_KEY_OUT                      0xA0
#   define HID_KEY_OPER                     0xA1
#   define HID_KEY_CLEAR_AGAIN              0xA2
#   define HID_KEY_CRSEL_PROPS              0xA3
#   define HID_KEY_EXSEL                    0xA4
#   define HID_KEY_CONTROL_LEFT             0xE0
#   define HID_KEY_SHIFT_LEFT               0xE1
#   define HID_KEY_ALT_LEFT                 0xE2
#   define HID_KEY_GUI_LEFT                 0xE3
#   define HID_KEY_CONTROL_RIGHT            0xE4
#   define HID_KEY_SHIFT_RIGHT              0xE5
#   define HID_KEY_ALT_RIGHT                0xE6
#   define HID_KEY_GUI_RIGHT                0xE7
#endif // HID_KEY_NONE

#ifndef HID_KEY_ERROR_UNDEFINED
#   define HID_KEY_ERROR_UNDEFINED          0x03
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t modifier;
#define KEYBD_MOD_LCTRL                     BIT0
#define KEYBD_MOD_LSHIFT                    BIT1
#define KEYBD_MOD_LALT                      BIT2
#define KEYBD_MOD_LGUI                      BIT3
#define KEYBD_MOD_RCTRL                     BIT4
#define KEYBD_MOD_RSHIFT                    BIT5
#define KEYBD_MOD_RALT                      BIT6
#define KEYBD_MOD_RGUI                      BIT7
#define KEYBD_MOD_HAS_SHIFT(mod)            ( (mod) & (BIT1 | BIT5) )
#define KEYBD_MOD_ADD_SHIFT(mod)            ( (mod) | (BIT1 | BIT5) )
#define KEYBD_MOD_DEL_SHIFT(mod)            ( (mod) & ~(BIT1 | BIT5) )
    uint8_t reserved;
    uint8_t keycode[6];                             // array of HID_KEY_XXX
} PACKED hid_keybd_report_t;

typedef struct {
    uint8_t numlock : 1;
    uint8_t capslock : 1;
    uint8_t scrolllock : 1;
    uint8_t compose : 1;
    uint8_t kana : 1;
    uint8_t : 3;
} PACKED hid_keybd_output_t;

#if !defined(WITH_TUSB) || defined(DUAL_TUSB)
typedef struct {
    uint8_t buttons;
#   define MOUSE_BUTTON_LEFT                BIT0
#   define MOUSE_BUTTON_RIGHT               BIT1
#   define MOUSE_BUTTON_MIDDLE              BIT2
#   define MOUSE_BUTTON_BACKWARD            BIT3
#   define MOUSE_BUTTON_FORWARD             BIT4
    int8_t x, y, wheel, pan;
} PACKED hid_mouse_report_t;
#endif

typedef struct {
    uint8_t buttons;
    uint16_t x, y;
    int8_t wheel, pan;
} PACKED hid_abmse_report_t;

typedef struct {
    uint8_t tip : 1;
    uint8_t rng : 1;
    uint8_t : 6;
    uint16_t x, y;
} PACKED hid_point_report_t;

typedef struct {
    struct {
        uint8_t tip : 1;                            // whether touched
        uint8_t : 3;
        uint8_t cid : 4;                            // 0-9 finger id
        uint16_t x, y;                              // 0~10000 percentage
    } PACKED fingers[5];
    uint16_t rate;                                  // sample rate
    uint8_t count;                                  // 0-9 number of touched
} PACKED hid_touch_report_t;

typedef struct {
    int16_t lx, ly, rx, ry;                         // -32767~32767
    uint8_t lt, rt;                                 // 0~255
    uint8_t dpad;                                   // 0~8 4-bit hat x 2
    uint16_t btns;
#define GMPAD_BUTTON_A                      BIT0
#define GMPAD_BUTTON_B                      BIT1
#define GMPAD_BUTTON_X                      BIT2
#define GMPAD_BUTTON_Y                      BIT3
#define GMPAD_BUTTON_LB                     BIT4    // L2: left shoulder
#define GMPAD_BUTTON_RB                     BIT5    // R2: right shoulder
#define GMPAD_BUTTON_LS                     BIT6    // L3: left joystick
#define GMPAD_BUTTON_RS                     BIT7    // R3: right joystick
#define GMPAD_BUTTON_PREV                   BIT8    // back / select
#define GMPAD_BUTTON_NEXT                   BIT9    // start
#define GMPAD_BUTTON_HOME                   BIT10   // xbox
#define GMPAD_BUTTON_SHARE                  BIT11
#define GMPAD_BUTTON_U                      BIT12
#define GMPAD_BUTTON_R                      BIT13
#define GMPAD_BUTTON_D                      BIT14
#define GMPAD_BUTTON_L                      BIT15
} hid_gmpad_data_t; // used to generate hid_gmpad_report_t according to layout

typedef union {
    struct {
        int8_t lx, ly, lz, rx, ry, rz;              // -127~127
        uint8_t dpad;
        uint16_t btns;
    } PACKED general;
    struct {
        uint16_t lx, ly, rx, ry;                    // 0~65535
        uint16_t lt, rt;                            // 0~1023 + 6-bit padding
        uint8_t dpad;                               // 1~8    + 4-bit padding
        uint16_t btns;                              // 11 btn + 5-bit padding
        uint8_t share;                              // 1 btn  + 7-bit padding
    } PACKED xinput;
    struct {
        uint16_t btns;                              // 14 btn + 2-bit padding
        uint8_t dpad;                               // 1~8    + 4-bit padding
        uint16_t lx, ly, rx, ry;                    // 0~65535
    } PACKED nswitch;
    struct {
        uint8_t lx, ly, rx, ry;                     // 0~255
        uint8_t dpad;                               // 1~8    + 4-bit ABXY
        uint16_t btns;                              // 10 btn + 6-bit padding
        uint8_t lt, rt;                             // 0~255
    } PACKED dsense;
} hid_gmpad_report_t;

typedef struct {
    uint8_t enabled;                                // 0 or 1
    uint8_t mag_left;                               // 0~100
    uint8_t mag_right;
    uint8_t mag_strong;
    uint8_t mag_weak;
    uint8_t duration;                               // 0~255 unit: 10ms
    uint8_t start_delay;                            // 0~255 unit: 10ms
    uint8_t loop_count;                             // 0~255
} PACKED hid_gmpad_output_xinput_t;

enum {
    REPORT_ID_KEYBD = 1,
    REPORT_ID_MOUSE = 2,
    REPORT_ID_ABMSE,
    REPORT_ID_POINT,
    REPORT_ID_TOUCH,
    REPORT_ID_GMPAD,
    REPORT_ID_SCTRL,
    REPORT_ID_SDIAL,
    REPORT_ID_MAX
};

typedef struct {
    uint8_t pad;                                    // hid_gmpad_layout_t
    uint8_t rlen[REPORT_ID_MAX];                    // report packet length
    uint8_t desc[512];                              // HID descriptor
    uint16_t dlen;                                  // HID descriptor length
    uint16_t vid, pid, ver;                         // vendor, product, version
    char dstr[128];                                 // device description
    const char *vendor, *serial;                    // manufacturer, device uuid
} hidtool_t;

extern hidtool_t HIDTool;

void hidtool_initialize();  // calculate HIDTool from Config.app.HID_MODE

typedef enum {
    HID_TARGET_USB = 0x01,  // USB Device | USB Host
    HID_TARGET_BLE = 0x02,  // BT Device | BT Host
    HID_TARGET_UDP = 0x04,  // UDP sendto host | broadcast
    HID_TARGET_SCN = 0x08,  // Screen input device
    HID_TARGET_ALL = 0xFF,
} hid_target_t;         // interface the HID report is sent to or received from

typedef struct {
    union {
        hid_keybd_report_t keybd;
        hid_mouse_report_t mouse;
        hid_abmse_report_t abmse;
        hid_point_report_t point;
        hid_touch_report_t touch;
        hid_gmpad_report_t gmpad;
        uint8_t sctrl;
        uint8_t sdial[2];
    };
    uint8_t id, size;
#define HID_VALID_REPORT(p)                                                 \
    ( (p) && (p)->size && (p)->id && (p)->id < REPORT_ID_MAX )
} hid_report_t;

bool hid_report_send(hid_target_t, hid_report_t *);

/*
 * Keyboard
 */

const char * hid_keycode_str(uint8_t keycode, uint8_t modifier);
const char * hid_keycodes_str(const uint8_t keycodes[6], uint8_t modifier);
const char * hid_modifier_str(uint8_t modifier);
bool hid_report_keybd(hid_target_t, uint8_t m, const uint8_t *kc, size_t l);
bool hid_report_keybd_press(hid_target_t, const char *str, uint32_t ms);

/*
 * Mouse (relative and absolute)
 */

const char * hid_btncode_str(uint8_t btns);
bool hid_report_mouse(hid_target_t, uint8_t, int8_t, int8_t, int8_t, int8_t);
bool hid_report_mouse_click(hid_target_t, const char *str, uint32_t ms);
bool hid_report_mouse_moveto(hid_target_t, uint16_t, uint16_t);
#define hid_report_mouse_move(t, x, y)   hid_report_mouse((t), 0, (x), (y), 0, 0)
#define hid_report_mouse_scroll(t, v, h) hid_report_mouse((t), 0, 0, 0, (v), (h))
#define hid_report_mouse_button(t, btn)  hid_report_mouse((t), (btn), 0, 0, 0, 0)

/*
 * Gamepad
 */

typedef enum {
    GMPAD_GENERAL = 1,  // 16 buttons + 2 dpads + 6 axes
    GMPAD_XINPUT,       // 16 buttons + 1 dpad + 2 triggers + 4 axes
    GMPAD_SWITCH,
    GMPAD_DSENSE,
} hid_gmpad_layout_t;

typedef enum {
    GMPAD_DPAD_NONE,
    GMPAD_DPAD_U,
    GMPAD_DPAD_UR,
    GMPAD_DPAD_R,
    GMPAD_DPAD_DR,
    GMPAD_DPAD_D,
    GMPAD_DPAD_DL,
    GMPAD_DPAD_L,
    GMPAD_DPAD_UL,
    GMPAD_DPAD_MAX,
} hid_gmpad_dpad_t;

bool hid_report_gmpad_dpad(hid_target_t, hid_gmpad_dpad_t, hid_gmpad_dpad_t);
bool hid_report_gmpad_trig(hid_target_t, uint8_t lt, uint8_t rt);
bool hid_report_gmpad_joyst(hid_target_t, int16_t, int16_t, int16_t, int16_t);
bool hid_report_gmpad_click(hid_target_t, const char *str, uint32_t ms);
bool hid_report_gmpad_button(hid_target_t, uint16_t btn, uint8_t action);
#define hid_report_gmpad_btn_del(t, btn) hid_report_gmpad_button((t), (btn), 0)
#define hid_report_gmpad_btn_add(t, btn) hid_report_gmpad_button((t), (btn), 1)
#define hid_report_gmpad_btn_tog(t, btn) hid_report_gmpad_button((t), (btn), 2)
#define hid_report_gmpad_btn_set(t, btn) hid_report_gmpad_button((t), (btn), 3)

/*
 * System control
 */

typedef enum {
    // see HID_USAGE_DESKTOP_SYSTEM_CONTROL in hiddesc.h
    SCTRL_PWDN  = 0x01, // shutdown
    SCTRL_SLEEP = 0x02, // sleep
    SCTRL_WAKE  = 0x03, // wakeup
    SCTRL_MCTX  = 0x04, // menu context
    SCTRL_MMAIN = 0x05, // menu main
    SCTRL_MAPP  = 0x06, // menu app
    SCTRL_MHELP = 0x07, // menu help
    SCTRL_MEXIT = 0x08, // menu exit
    SCTRL_MSEL  = 0x09, // menu select
    SCTRL_MLT   = 0x0A, // menu left
    SCTRL_MRT   = 0x0B, // menu right
    SCTRL_MUP   = 0x0C, // menu up
    SCTRL_MDN   = 0x0D, // menu down
    SCTRL_RCOLD = 0x0E, // cold restart
    SCTRL_RWARM = 0x0F, // warm restart
    SCTRL_DPADU = 0x10, // dpad up
    SCTRL_DPADD = 0x11, // dpad down
    SCTRL_DPADR = 0x12, // dpad right
    SCTRL_DPADL = 0x13, // dpad left
} hid_sctrl_keycode_t;

bool hid_report_sctrl(hid_target_t, hid_sctrl_keycode_t);

/*
 * Surface Dial
 */

typedef enum {
    SDIAL_U = 0x00,     // button release
    SDIAL_D = 0x01,     // button press
    SDIAL_L = 0x38,     // knob rotate ccw
    SDIAL_R = 0xC8,     // knob rotate cw
} hid_sdial_keycode_t;

bool hid_report_sdial(hid_target_t, hid_sdial_keycode_t);
bool hid_report_sdial_click(hid_target_t, uint32_t ms);

/*
 * Callback functions
 */

typedef void (*hid_key_cb)(uint8_t keycode, bool pressed);
typedef void (*hid_pos_cb)(int x, int y, int8_t dx, int8_t dy);
void hid_handle_mouse(hid_target_t, hid_mouse_report_t *, hid_key_cb, hid_pos_cb);
void hid_handle_abmse(hid_target_t, hid_abmse_report_t *, hid_key_cb, hid_pos_cb);
void hid_handle_keybd(hid_target_t, hid_keybd_report_t *, hid_key_cb);

#ifdef __cplusplus
}
#endif
