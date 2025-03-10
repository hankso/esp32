/*
 * File: hidtool.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/17 12:56:20
 */

#include "hidtool.h"
#include "usbmode.h"
#include "btmode.h"
#include "screen.h"
#include "config.h"

#ifdef WITH_TUSB
#   include "tusb.h"
#   include "class/hid/hid.h"
#   include "class/hid/hid_device.h"
#else

// Copied from tinyusb-v0.9.0/src/class/hid/hid.h

//------------- ITEM & TAG -------------//
#   define HID_REPORT_DATA_0(data)
#   define HID_REPORT_DATA_1(data)  , data
#   define HID_REPORT_DATA_2(data)  , (data) & 0xFF, ((data) >> 8) & 0xFF

#   define HID_REPORT_ITEM(data, tag, type, size) \
           (((tag) << 4) | ((type) << 2) | (size)) HID_REPORT_DATA_##size(data)

#   define RI_TYPE_MAIN             0
#   define RI_TYPE_GLOBAL           1
#   define RI_TYPE_LOCAL            2

//------------- MAIN ITEMS 6.2.2.4 -------------//
#   define HID_INPUT(x)             HID_REPORT_ITEM(x,  8, RI_TYPE_MAIN, 1)
#   define HID_OUTPUT(x)            HID_REPORT_ITEM(x,  9, RI_TYPE_MAIN, 1)
#   define HID_COLLECTION(x)        HID_REPORT_ITEM(x, 10, RI_TYPE_MAIN, 1)
#   define HID_FEATURE(x)           HID_REPORT_ITEM(x, 11, RI_TYPE_MAIN, 1)
#   define HID_COLLECTION_END       HID_REPORT_ITEM(x, 12, RI_TYPE_MAIN, 0)

//------------- INPUT, OUTPUT, FEATURE 6.2.2.5 -------------//
#   define HID_DATA                 (0<<0)
#   define HID_CONSTANT             (1<<0)
#   define HID_ARRAY                (0<<1)
#   define HID_VARIABLE             (1<<1)
#   define HID_ABSOLUTE             (0<<2)
#   define HID_RELATIVE             (1<<2)
#   define HID_WRAP_NO              (0<<3)
#   define HID_WRAP                 (1<<3)
#   define HID_LINEAR               (0<<4)
#   define HID_NONLINEAR            (1<<4)
#   define HID_PREFERRED_STATE      (0<<5)
#   define HID_PREFERRED_NO         (1<<5)
#   define HID_NO_NULL_POSITION     (0<<6)
#   define HID_NULL_STATE           (1<<6)
#   define HID_NON_VOLATILE         (0<<7)
#   define HID_VOLATILE             (1<<7)
#   define HID_BITFIELD             (0<<8)
#   define HID_BUFFERED_BYTES       (1<<8)

//------------- COLLECTION ITEM 6.2.2.6 -------------//
enum {
    HID_COLLECTION_PHYSICAL = 0,
    HID_COLLECTION_APPLICATION,
    HID_COLLECTION_LOGICAL,
    HID_COLLECTION_REPORT,
    HID_COLLECTION_NAMED_ARRAY,
    HID_COLLECTION_USAGE_SWITCH,
    HID_COLLECTION_USAGE_MODIFIER
};

//------------- GLOBAL ITEMS 6.2.2.7 -------------//
#   define HID_USAGE_PAGE(x)        HID_REPORT_ITEM(x, 0, RI_TYPE_GLOBAL, 1)
#   define HID_USAGE_PAGE_N(x, n)   HID_REPORT_ITEM(x, 0, RI_TYPE_GLOBAL, n)
#   define HID_LOGICAL_MIN(x)       HID_REPORT_ITEM(x, 1, RI_TYPE_GLOBAL, 1)
#   define HID_LOGICAL_MIN_N(x, n)  HID_REPORT_ITEM(x, 1, RI_TYPE_GLOBAL, n)
#   define HID_LOGICAL_MAX(x)       HID_REPORT_ITEM(x, 2, RI_TYPE_GLOBAL, 1)
#   define HID_LOGICAL_MAX_N(x, n)  HID_REPORT_ITEM(x, 2, RI_TYPE_GLOBAL, n)
#   define HID_PHYSICAL_MIN(x)      HID_REPORT_ITEM(x, 3, RI_TYPE_GLOBAL, 1)
#   define HID_PHYSICAL_MIN_N(x, n) HID_REPORT_ITEM(x, 3, RI_TYPE_GLOBAL, n)
#   define HID_PHYSICAL_MAX(x)      HID_REPORT_ITEM(x, 4, RI_TYPE_GLOBAL, 1)
#   define HID_PHYSICAL_MAX_N(x, n) HID_REPORT_ITEM(x, 4, RI_TYPE_GLOBAL, n)
#   define HID_UNIT_EXPONENT(x)     HID_REPORT_ITEM(x, 5, RI_TYPE_GLOBAL, 1)
#   define HID_UNIT_EXPONENT_N(x, n) HID_REPORT_ITEM(x, 5, RI_TYPE_GLOBAL, n)
#   define HID_UNIT(x)              HID_REPORT_ITEM(x, 6, RI_TYPE_GLOBAL, 1)
#   define HID_UNIT_N(x, n)         HID_REPORT_ITEM(x, 6, RI_TYPE_GLOBAL, n)
#   define HID_REPORT_SIZE(x)       HID_REPORT_ITEM(x, 7, RI_TYPE_GLOBAL, 1)
#   define HID_REPORT_SIZE_N(x, n)  HID_REPORT_ITEM(x, 7, RI_TYPE_GLOBAL, n)
#   define HID_REPORT_ID(x)         HID_REPORT_ITEM(x, 8, RI_TYPE_GLOBAL, 1),
#   define HID_REPORT_ID_N(x)       HID_REPORT_ITEM(x, 8, RI_TYPE_GLOBAL, n),
#   define HID_REPORT_COUNT(x)      HID_REPORT_ITEM(x, 9, RI_TYPE_GLOBAL, 1)
#   define HID_REPORT_COUNT_N(x, n) HID_REPORT_ITEM(x, 9, RI_TYPE_GLOBAL, n)
#   define HID_PUSH                 HID_REPORT_ITEM(x, 10, RI_TYPE_GLOBAL, 0)
#   define HID_POP                  HID_REPORT_ITEM(x, 11, RI_TYPE_GLOBAL, 0)
#   define HID_USAGE(x)             HID_REPORT_ITEM(x, 0, RI_TYPE_LOCAL, 1)
#   define HID_USAGE_N(x, n)        HID_REPORT_ITEM(x, 0, RI_TYPE_LOCAL, n)
#   define HID_USAGE_MIN(x)         HID_REPORT_ITEM(x, 1, RI_TYPE_LOCAL, 1)
#   define HID_USAGE_MIN_N(x, n)    HID_REPORT_ITEM(x, 1, RI_TYPE_LOCAL, n)
#   define HID_USAGE_MAX(x)         HID_REPORT_ITEM(x, 2, RI_TYPE_LOCAL, 1)
#   define HID_USAGE_MAX_N(x, n)    HID_REPORT_ITEM(x, 2, RI_TYPE_LOCAL, n)

/// HID Usage Table - Table 1: Usage Page Summary
enum {
    HID_USAGE_PAGE_DESKTOP         = 0x01,
    HID_USAGE_PAGE_SIMULATE        = 0x02,
    HID_USAGE_PAGE_VIRTUAL_REALITY = 0x03,
    HID_USAGE_PAGE_SPORT           = 0x04,
    HID_USAGE_PAGE_GAME            = 0x05,
    HID_USAGE_PAGE_GENERIC_DEVICE  = 0x06,
    HID_USAGE_PAGE_KEYBOARD        = 0x07,
    HID_USAGE_PAGE_LED             = 0x08,
    HID_USAGE_PAGE_BUTTON          = 0x09,
    HID_USAGE_PAGE_ORDINAL         = 0x0a,
    HID_USAGE_PAGE_TELEPHONY       = 0x0b,
    HID_USAGE_PAGE_CONSUMER        = 0x0c,
    HID_USAGE_PAGE_DIGITIZER       = 0x0d,
    HID_USAGE_PAGE_PID             = 0x0f,
    HID_USAGE_PAGE_UNICODE         = 0x10,
    HID_USAGE_PAGE_ALPHA_DISPLAY   = 0x14,
    HID_USAGE_PAGE_MEDICAL         = 0x40,
    HID_USAGE_PAGE_MONITOR         = 0x80, //0x80 - 0x83
    HID_USAGE_PAGE_POWER           = 0x84, // 0x084 - 0x87
    HID_USAGE_PAGE_BARCODE_SCANNER = 0x8c,
    HID_USAGE_PAGE_SCALE           = 0x8d,
    HID_USAGE_PAGE_MSR             = 0x8e,
    HID_USAGE_PAGE_CAMERA          = 0x90,
    HID_USAGE_PAGE_ARCADE          = 0x91,
    HID_USAGE_PAGE_VENDOR          = 0xFF00 // 0xFF00 - 0xFFFF
};

/// HID Usage Table - Table 6: Generic Desktop Page
enum {
    HID_USAGE_DESKTOP_POINTER                               = 0x01,
    HID_USAGE_DESKTOP_MOUSE                                 = 0x02,
    HID_USAGE_DESKTOP_JOYSTICK                              = 0x04,
    HID_USAGE_DESKTOP_GAMEPAD                               = 0x05,
    HID_USAGE_DESKTOP_KEYBOARD                              = 0x06,
    HID_USAGE_DESKTOP_KEYPAD                                = 0x07,
    HID_USAGE_DESKTOP_MULTI_AXIS_CONTROLLER                 = 0x08,
    HID_USAGE_DESKTOP_TABLET_PC_SYSTEM                      = 0x09,
    HID_USAGE_DESKTOP_X                                     = 0x30,
    HID_USAGE_DESKTOP_Y                                     = 0x31,
    HID_USAGE_DESKTOP_Z                                     = 0x32,
    HID_USAGE_DESKTOP_RX                                    = 0x33,
    HID_USAGE_DESKTOP_RY                                    = 0x34,
    HID_USAGE_DESKTOP_RZ                                    = 0x35,
    HID_USAGE_DESKTOP_SLIDER                                = 0x36,
    HID_USAGE_DESKTOP_DIAL                                  = 0x37,
    HID_USAGE_DESKTOP_WHEEL                                 = 0x38,
    HID_USAGE_DESKTOP_HAT_SWITCH                            = 0x39,
    HID_USAGE_DESKTOP_COUNTED_BUFFER                        = 0x3a,
    HID_USAGE_DESKTOP_BYTE_COUNT                            = 0x3b,
    HID_USAGE_DESKTOP_MOTION_WAKEUP                         = 0x3c,
    HID_USAGE_DESKTOP_START                                 = 0x3d,
    HID_USAGE_DESKTOP_SELECT                                = 0x3e,
    HID_USAGE_DESKTOP_VX                                    = 0x40,
    HID_USAGE_DESKTOP_VY                                    = 0x41,
    HID_USAGE_DESKTOP_VZ                                    = 0x42,
    HID_USAGE_DESKTOP_VBRX                                  = 0x43,
    HID_USAGE_DESKTOP_VBRY                                  = 0x44,
    HID_USAGE_DESKTOP_VBRZ                                  = 0x45,
    HID_USAGE_DESKTOP_VNO                                   = 0x46,
    HID_USAGE_DESKTOP_FEATURE_NOTIFICATION                  = 0x47,
    HID_USAGE_DESKTOP_RESOLUTION_MULTIPLIER                 = 0x48,
    HID_USAGE_DESKTOP_SYSTEM_CONTROL                        = 0x80,
    HID_USAGE_DESKTOP_SYSTEM_POWER_DOWN                     = 0x81,
    HID_USAGE_DESKTOP_SYSTEM_SLEEP                          = 0x82,
    HID_USAGE_DESKTOP_SYSTEM_WAKE_UP                        = 0x83,
    HID_USAGE_DESKTOP_SYSTEM_CONTEXT_MENU                   = 0x84,
    HID_USAGE_DESKTOP_SYSTEM_MAIN_MENU                      = 0x85,
    HID_USAGE_DESKTOP_SYSTEM_APP_MENU                       = 0x86,
    HID_USAGE_DESKTOP_SYSTEM_MENU_HELP                      = 0x87,
    HID_USAGE_DESKTOP_SYSTEM_MENU_EXIT                      = 0x88,
    HID_USAGE_DESKTOP_SYSTEM_MENU_SELECT                    = 0x89,
    HID_USAGE_DESKTOP_SYSTEM_MENU_RIGHT                     = 0x8A,
    HID_USAGE_DESKTOP_SYSTEM_MENU_LEFT                      = 0x8B,
    HID_USAGE_DESKTOP_SYSTEM_MENU_UP                        = 0x8C,
    HID_USAGE_DESKTOP_SYSTEM_MENU_DOWN                      = 0x8D,
    HID_USAGE_DESKTOP_SYSTEM_COLD_RESTART                   = 0x8E,
    HID_USAGE_DESKTOP_SYSTEM_WARM_RESTART                   = 0x8F,
    HID_USAGE_DESKTOP_DPAD_UP                               = 0x90,
    HID_USAGE_DESKTOP_DPAD_DOWN                             = 0x91,
    HID_USAGE_DESKTOP_DPAD_RIGHT                            = 0x92,
    HID_USAGE_DESKTOP_DPAD_LEFT                             = 0x93,
    HID_USAGE_DESKTOP_SYSTEM_DOCK                           = 0xA0,
    HID_USAGE_DESKTOP_SYSTEM_UNDOCK                         = 0xA1,
    HID_USAGE_DESKTOP_SYSTEM_SETUP                          = 0xA2,
    HID_USAGE_DESKTOP_SYSTEM_BREAK                          = 0xA3,
    HID_USAGE_DESKTOP_SYSTEM_DEBUGGER_BREAK                 = 0xA4,
    HID_USAGE_DESKTOP_APPLICATION_BREAK                     = 0xA5,
    HID_USAGE_DESKTOP_APPLICATION_DEBUGGER_BREAK            = 0xA6,
    HID_USAGE_DESKTOP_SYSTEM_SPEAKER_MUTE                   = 0xA7,
    HID_USAGE_DESKTOP_SYSTEM_HIBERNATE                      = 0xA8,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_INVERT                 = 0xB0,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_INTERNAL               = 0xB1,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_EXTERNAL               = 0xB2,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_BOTH                   = 0xB3,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_DUAL                   = 0xB4,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_TOGGLE_INT_EXT         = 0xB5,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_SWAP_PRIMARY_SECONDARY = 0xB6,
    HID_USAGE_DESKTOP_SYSTEM_DISPLAY_LCD_AUTOSCALE          = 0xB7
};

/// HID Usage Table: Consumer Page (0x0C)
/// Only contains controls that supported by Windows (whole list is too long)
enum {
    // Generic Control
    HID_USAGE_CONSUMER_CONTROL                           = 0x0001,

    // Power Control
    HID_USAGE_CONSUMER_POWER                             = 0x0030,
    HID_USAGE_CONSUMER_RESET                             = 0x0031,
    HID_USAGE_CONSUMER_SLEEP                             = 0x0032,

    // Screen Brightness
    HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT              = 0x006F,
    HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT              = 0x0070,

    // These HID usages operate only on mobile systems (battery powered) and
    // require Windows 8 (build 8302 or greater).
    HID_USAGE_CONSUMER_WIRELESS_RADIO_CONTROLS           = 0x000C,
    HID_USAGE_CONSUMER_WIRELESS_RADIO_BUTTONS            = 0x00C6,
    HID_USAGE_CONSUMER_WIRELESS_RADIO_LED                = 0x00C7,
    HID_USAGE_CONSUMER_WIRELESS_RADIO_SLIDER_SWITCH      = 0x00C8,

    // Media Control
    HID_USAGE_CONSUMER_PLAY_PAUSE                        = 0x00CD,
    HID_USAGE_CONSUMER_SCAN_NEXT                         = 0x00B5,
    HID_USAGE_CONSUMER_SCAN_PREVIOUS                     = 0x00B6,
    HID_USAGE_CONSUMER_STOP                              = 0x00B7,
    HID_USAGE_CONSUMER_VOLUME                            = 0x00E0,
    HID_USAGE_CONSUMER_MUTE                              = 0x00E2,
    HID_USAGE_CONSUMER_BASS                              = 0x00E3,
    HID_USAGE_CONSUMER_TREBLE                            = 0x00E4,
    HID_USAGE_CONSUMER_BASS_BOOST                        = 0x00E5,
    HID_USAGE_CONSUMER_VOLUME_INCREMENT                  = 0x00E9,
    HID_USAGE_CONSUMER_VOLUME_DECREMENT                  = 0x00EA,
    HID_USAGE_CONSUMER_BASS_INCREMENT                    = 0x0152,
    HID_USAGE_CONSUMER_BASS_DECREMENT                    = 0x0153,
    HID_USAGE_CONSUMER_TREBLE_INCREMENT                  = 0x0154,
    HID_USAGE_CONSUMER_TREBLE_DECREMENT                  = 0x0155,

    // Application Launcher
    HID_USAGE_CONSUMER_AL_CONSUMER_CONTROL_CONFIGURATION = 0x0183,
    HID_USAGE_CONSUMER_AL_EMAIL_READER                   = 0x018A,
    HID_USAGE_CONSUMER_AL_CALCULATOR                     = 0x0192,
    HID_USAGE_CONSUMER_AL_LOCAL_BROWSER                  = 0x0194,

    // Browser/Explorer Specific
    HID_USAGE_CONSUMER_AC_SEARCH                         = 0x0221,
    HID_USAGE_CONSUMER_AC_HOME                           = 0x0223,
    HID_USAGE_CONSUMER_AC_BACK                           = 0x0224,
    HID_USAGE_CONSUMER_AC_FORWARD                        = 0x0225,
    HID_USAGE_CONSUMER_AC_STOP                           = 0x0226,
    HID_USAGE_CONSUMER_AC_REFRESH                        = 0x0227,
    HID_USAGE_CONSUMER_AC_BOOKMARKS                      = 0x022A,

    // Mouse Horizontal scroll
    HID_USAGE_CONSUMER_AC_PAN                            = 0x0238,
};

// Copied from tinyusb/class/hid/hid_device.h
#   define TUD_HID_REPORT_DESC_KEYBOARD(...)                                \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP     )                          ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_KEYBOARD )                          ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION )                          ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        /* 8 bits Modifier Keys (Shfit, Control, Alt, GUI) */               \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_KEYBOARD )                         ,\
            HID_USAGE_MIN    ( 224                                       ) ,\
            HID_USAGE_MAX    ( 231                                       ) ,\
            HID_LOGICAL_MIN  ( 0                                         ) ,\
            HID_LOGICAL_MAX  ( 1                                         ) ,\
            HID_REPORT_COUNT ( 8                                         ) ,\
            HID_REPORT_SIZE  ( 1                                         ) ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE    ) ,\
            /* 8 bit reserved */                                            \
            HID_REPORT_COUNT ( 1                                         ) ,\
            HID_REPORT_SIZE  ( 8                                         ) ,\
            HID_INPUT        ( HID_CONSTANT                              ) ,\
        /* 6-byte Keycodes */                                               \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_KEYBOARD )                         ,\
            HID_USAGE_MIN    ( 0                                         ) ,\
            HID_USAGE_MAX    ( 255                                       ) ,\
            HID_LOGICAL_MIN  ( 0                                         ) ,\
            HID_LOGICAL_MAX  ( 255                                       ) ,\
            HID_REPORT_COUNT ( 6                                         ) ,\
            HID_REPORT_SIZE  ( 8                                         ) ,\
            HID_INPUT        ( HID_DATA | HID_ARRAY | HID_ABSOLUTE       ) ,\
        /* 5-bit LED Kana | Compose | ScrollLock | CapsLock | NumLock */    \
        HID_USAGE_PAGE  ( HID_USAGE_PAGE_LED )                             ,\
            HID_USAGE_MIN    ( 1                                         ) ,\
            HID_USAGE_MAX    ( 5                                         ) ,\
            HID_REPORT_COUNT ( 5                                         ) ,\
            HID_REPORT_SIZE  ( 1                                         ) ,\
            HID_OUTPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE    ) ,\
            /* led padding */                                               \
            HID_REPORT_COUNT ( 1                                         ) ,\
            HID_REPORT_SIZE  ( 3                                         ) ,\
            HID_OUTPUT       ( HID_CONSTANT                              ) ,\
    HID_COLLECTION_END

#   define TUD_HID_REPORT_DESC_MOUSE(...)                                   \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP      )                         ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_MOUSE     )                         ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION  )                         ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_USAGE      ( HID_USAGE_DESKTOP_POINTER )                       ,\
        HID_COLLECTION ( HID_COLLECTION_PHYSICAL   )                       ,\
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_BUTTON  )                     ,\
                HID_USAGE_MIN   ( 1                                      ) ,\
                HID_USAGE_MAX   ( 5                                      ) ,\
                HID_LOGICAL_MIN ( 0                                      ) ,\
                HID_LOGICAL_MAX ( 1                                      ) ,\
                /* Left, Right, Middle, Backward, Forward buttons */        \
                HID_REPORT_COUNT( 5                                      ) ,\
                HID_REPORT_SIZE ( 1                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
                /* 3 bit padding */                                         \
                HID_REPORT_COUNT( 1                                      ) ,\
                HID_REPORT_SIZE ( 3                                      ) ,\
                HID_INPUT       ( HID_CONSTANT                           ) ,\
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP )                     ,\
                /* X, Y position [-127, 127] */                             \
                HID_USAGE       ( HID_USAGE_DESKTOP_X                    ) ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_Y                    ) ,\
                HID_LOGICAL_MIN ( 0x81                                   ) ,\
                HID_LOGICAL_MAX ( 0x7f                                   ) ,\
                HID_REPORT_COUNT( 2                                      ) ,\
                HID_REPORT_SIZE ( 8                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
                /* Verital wheel scroll [-127, 127] */                      \
                HID_USAGE       ( HID_USAGE_DESKTOP_WHEEL                ) ,\
                HID_LOGICAL_MIN ( 0x81                                   ) ,\
                HID_LOGICAL_MAX ( 0x7f                                   ) ,\
                HID_REPORT_COUNT( 1                                      ) ,\
                HID_REPORT_SIZE ( 8                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_CONSUMER ),                    \
                /* Horizontal wheel scroll [-127, 127] */                   \
                HID_USAGE_N     ( HID_USAGE_CONSUMER_AC_PAN, 2           ) ,\
                HID_LOGICAL_MIN ( 0x81                                   ) ,\
                HID_LOGICAL_MAX ( 0x7f                                   ) ,\
                HID_REPORT_COUNT( 1                                      ) ,\
                HID_REPORT_SIZE ( 8                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
        HID_COLLECTION_END                                                 ,\
    HID_COLLECTION_END

#endif // WITH_TUSB

#define TUD_HID_REPORT_DESC_DIAL(...)                                       \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                              ,\
    HID_USAGE      ( 0x0E )                                                ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION )                          ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER )                        ,\
        HID_USAGE      ( 0x21 )                                            ,\
        HID_COLLECTION ( HID_COLLECTION_PHYSICAL )                         ,\
            HID_USAGE_PAGE   ( HID_USAGE_PAGE_BUTTON )                     ,\
            HID_USAGE        ( 1 )                                         ,\
            HID_REPORT_COUNT ( 1                                         ) ,\
            HID_REPORT_SIZE  ( 1                                         ) ,\
            HID_LOGICAL_MIN  ( 0                                         ) ,\
            HID_LOGICAL_MAX  ( 1                                         ) ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE    ) ,\
            HID_USAGE_PAGE   ( HID_USAGE_PAGE_DESKTOP )                    ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_DIAL )                    ,\
            HID_REPORT_COUNT ( 1                                         ) ,\
            HID_REPORT_SIZE  ( 15                                        ) ,\
            HID_UNIT_EXPONENT( 0x0F                                      ) ,\
                /* HID Unit: English Rotation - Angular Position */         \
                HID_UNIT           ( 0x14 )                                ,\
                HID_PHYSICAL_MIN_N ( -3600, 2                            ) ,\
                HID_PHYSICAL_MAX_N (  3600, 2                            ) ,\
                HID_LOGICAL_MIN_N  ( -3600, 2                            ) ,\
                HID_LOGICAL_MAX_N  (  3600, 2                            ) ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_RELATIVE    ) ,\
        HID_COLLECTION_END                                                 ,\
    HID_COLLECTION_END

const uint8_t hid_descriptor_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBD)),   // 65 Bytes
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),      // 79 Bytes
    TUD_HID_REPORT_DESC_DIAL(HID_REPORT_ID(REPORT_ID_DIAL)),        // 56 Bytes
};

const size_t hid_descriptor_report_len = sizeof(hid_descriptor_report); // 200

static const char *TAG = "HIDTool";

uint16_t hid_desc_version(uint16_t *arg) {
    uint16_t version = 0;
    int buf[3];
    if (parse_all(Config.info.VER, buf, sizeof(buf)) >= 2)
        version = ((buf[0] & 0xFF) << 8) | (buf[1] & 0xFF);
    if (version && arg) *arg = version;
    return version;
}

const char * hid_desc_vendor(const char **arg) {
#ifdef CONFIG_TINYUSB_DESC_MANUFACTURER_STRING
    const char *vendor = CONFIG_TINYUSB_DESC_MANUFACTURER_STRING;
#else
    const char *vendor = Config.info.NAME;
#endif
    if (strlen(vendor) && arg) *arg = vendor;
    return vendor;
}

const char * hid_desc_serial(const char **arg) {
    const char *serial = strlen(Config.info.UID ?: "") ? Config.info.UID : "";
    if (strlen(serial) && arg) *arg = serial;
    return serial;
}

bool hid_report_send(hid_target_t to, hid_report_t *report) {
    bool sent = false;
#ifdef CONFIG_BASE_USB_HID_DEVICE
    if (to == HID_TARGET_USB || to == HID_TARGET_ALL)
        sent |= hidu_send_report(report);
#endif
#ifdef CONFIG_BASE_USE_BT
    if (to == HID_TARGET_BLE || to == HID_TARGET_ALL)
        sent |= hidb_send_report(report);
#endif
#ifdef CONFIG_BASE_USE_SCREEN
    if (to == HID_TARGET_SCN || to == HID_TARGET_ALL)
        sent |= screen_command(SCN_INP, report) == ESP_OK;
#endif
    return sent;
}

/******************************************************************************
 * HID type: Surface Dial
 */

bool hid_report_dial(hid_target_t to, hid_dial_keycode_t k) {
    hid_report_t report = {
        .id = REPORT_ID_DIAL,
        .dial = { k, (k == DIAL_L) ? 0xFF : 0 }
    };
    return hid_report_send(to, &report);
}

bool hid_report_dial_button(hid_target_t to, uint32_t ms) {
    bool sent = hid_report_dial(to, DIAL_DN);
    if (sent && ms) {
        msleep(ms);
        sent = hid_report_dial(to, DIAL_UP);
    }
    return sent;
}

/******************************************************************************
 * HID type: Mouse move / click / scroll
 */

static const char * BUTTON_STR[] = {
    "Left", "Right", "Middle", "Backward", "Forward"
};

uint8_t str2btncode(const char *str) {
    LOOPN(i, LEN(BUTTON_STR)) {
        if (!strcasecmp(str, BUTTON_STR[i])) return 1 << i;
    }
    return 0;
}

const char *btncode2str(uint8_t btn) {
    LOOPN(i, LEN(BUTTON_STR)) {
        if (btn & (1 << i)) return BUTTON_STR[i];
    }
    return "Unknown";
}

const char * hid_btncode_str(uint8_t btns) {
    static char buf[64];
    size_t blen = sizeof(buf), size = 0;
    LOOPN(i, LEN(BUTTON_STR)) {
        if (btns & (1 << i)) {
            size += snprintf(buf + size, blen - size, "%s%s",
                i ? " | " : "", BUTTON_STR[i]);
        } else {
            buf[size] = '\0';
        }
    }
    return buf;
}

bool hid_report_mouse(
    hid_target_t to, uint8_t b, int8_t x, int8_t y, int8_t v, int8_t h
) {
    hid_report_t report = {
        .id = REPORT_ID_MOUSE,
        .mouse = { b, x, y, v, h }
    };
    return hid_report_send(to, &report);
}

bool hid_report_mouse_click(hid_target_t to, const char *str, uint32_t ms) {
    uint8_t btncode = str2btncode(str);
    bool sent = hid_report_mouse_button(to, btncode);
    if (sent && btncode && ms) {
        msleep(ms);
        sent = hid_report_mouse_button(to, 0);
    }
    return sent;
}

void hid_handle_mouse(
    hid_target_t from, hid_mouse_report_t *rpt,
    hid_key_cb key_cb, hid_pos_cb pos_cb
) {
    if (!rpt) return;
    static int xs[HID_TARGET_CNT], ys[HID_TARGET_CNT], btns[HID_TARGET_CNT];
    xs[from] += rpt->x;
    ys[from] += rpt->y;
    if (pos_cb) pos_cb(xs[from], ys[from], rpt->x, rpt->y);
    if (key_cb) LOOPN(i, 5) {
        uint8_t btn = BIT(i);
        if ((rpt->buttons & btn) == (btns[from] & btn)) continue;
        key_cb(btn, rpt->buttons & btn);
    }
    btns[from] = rpt->buttons;
    ESP_LOGI(TAG, "X: %06d Y: %06d |%c|%c|%c|", xs[from], ys[from],
             btns[from] & MOUSE_BUTTON_LEFT   ? 'L' : ' ',
             btns[from] & MOUSE_BUTTON_MIDDLE ? 'M' : ' ',
             btns[from] & MOUSE_BUTTON_RIGHT  ? 'R' : ' ');
}

/******************************************************************************
 * HID type: Keyboard press / release
 */

// HID_ASCII_TO_KEYCODE & KEYCODE_TO_ASCII (defined in tinyusb/class/hid/hid.h)
// occupies 512 bytes and does not provide info of keycodes above 0x7F.
// Here is our minimal implementation of conversion between ASCII and KEYCODE.

static const struct {
    uint8_t code;
    const char *name;       // see https://theasciicode.com.ar
} keycodes_special[] = {                    // ASCII
    { HID_KEY_HOME,         "Home" },       // 2 STX
    { HID_KEY_END,          "End" },        // 3 ETX
    { HID_KEY_BACKSPACE,    "Backspace" },  // 8
    { HID_KEY_TAB,          "Tab" },        // 9
    { HID_KEY_ENTER,        "CR" },         // 10 \n or 13 \r
    { HID_KEY_ARROW_UP,     "Up" },         // 17
    { HID_KEY_ARROW_DOWN,   "Down" },       // 18
    { HID_KEY_ARROW_RIGHT,  "Right" },      // 19
    { HID_KEY_ARROW_LEFT,   "Left" },       // 20
    { HID_KEY_CANCEL,       "Cancel" },     // 24
    { HID_KEY_ESCAPE,       "Escape" },     // 27
    { HID_KEY_SPACE,        "Space" },      // 32 (should not be "special")
    { HID_KEY_DELETE,       "Delete" },     // 127
    { HID_KEY_CAPS_LOCK,    "CapsLock" },
    { HID_KEY_PRINT_SCREEN, "PrtScn" },
    { HID_KEY_SCROLL_LOCK,  "ScrLock" },
    { HID_KEY_PAUSE,        "Pause" },
    { HID_KEY_INSERT,       "Insert" },
    { HID_KEY_PAGE_UP,      "PageUp" },
    { HID_KEY_PAGE_DOWN,    "PageDown" },
    { HID_KEY_NUM_LOCK,     "NumLock" },
    { HID_KEY_POWER,        "Power" },
};

static const uint8_t keycodes_normal[][3] = {
    // Keycodes                ASCII Shift
    { HID_KEY_1,                '1', '!' },
    { HID_KEY_2,                '2', '@' },
    { HID_KEY_3,                '3', '#' },
    { HID_KEY_4,                '4', '$' },
    { HID_KEY_5,                '5', '%' },
    { HID_KEY_6,                '6', '^' },
    { HID_KEY_7,                '7', '&' },
    { HID_KEY_8,                '8', '*' },
    { HID_KEY_9,                '9', '(' },
    { HID_KEY_0,                '0', ')' },
    { HID_KEY_SPACE,            ' ', ' ' },
    { HID_KEY_MINUS,            '-', '_' },
    { HID_KEY_EQUAL,            '=', '+' },
    { HID_KEY_BRACKET_LEFT,     '[', '{' },
    { HID_KEY_BRACKET_RIGHT,    ']', '}' },
    { HID_KEY_BACKSLASH,        '\\', '|' },
    { HID_KEY_EUROPE_1,         '\\', '|' },
    { HID_KEY_SEMICOLON,        ';', ':' },
    { HID_KEY_APOSTROPHE,       '\'', '"' },
    { HID_KEY_GRAVE,            '`', '~' },
    { HID_KEY_COMMA,            ',', '<' },
    { HID_KEY_PERIOD,           '.', '>' },
    { HID_KEY_SLASH,            '/', '?' },
};

static const char * modifier_names[] = {
    "L-Ctrl", "L-Shift", "L-Alt", "L-Win",
    "R-Ctrl", "R-Shift", "R-Alt", "R-Win",
      "Ctrl",   "Shift",   "Alt",   "Win",
};

uint8_t str2modifier(const char *str) {
    uint8_t mod = 0;
    LOOPN(i, LEN(modifier_names)) {
        if (strcasestr(str, modifier_names[i])) {
            mod |= 1 << (i % 8);
        }
    }
    return mod;
}

const uint8_t * str2keycodes(const char *str, uint8_t *mod) {
    static uint8_t buf[6];
    size_t len = strlen(str ?: ""), klen = 0, blen = sizeof(buf);
    if (!len) goto exit;
    if (str[0] == '|') {
        buf[klen++] = HID_KEY_BACKSLASH;
        if (mod) ADD_SHIFT(*mod);
    }
    char *dup = strdup(str);
    for (str = strtok(dup, "|"); str && klen < blen; str = strtok(NULL, "|")) {
        if (str2modifier(str)) continue;
        int fkey;
        bool has_fkey = parse_int(str + 1, &fkey) && fkey > 0 && fkey < 13;
        if (has_fkey && (str[0] == 'F' || str[0] == 'f')) {
            buf[klen++] = fkey - 1 + HID_KEY_F1;
            continue;
        }
        bool has_spec = false;
        LOOPN(i, LEN(keycodes_special)) {
            if (klen < blen && !strcasecmp(keycodes_special[i].name, str)) {
                buf[klen++] = keycodes_special[i].code;
                has_spec = true;
                break;
            }
        }
        if (has_spec) continue;
        LOOPN(i, LEN(keycodes_normal)) {
            if (klen < blen && str[0] == keycodes_normal[i][1])
                buf[klen++] = keycodes_normal[i][0];
            if (klen < blen && str[0] == keycodes_normal[i][2]) {
                buf[klen++] = keycodes_normal[i][0];
                if (mod) ADD_SHIFT(*mod);
            }
        }
        if ('a' <= str[0] && str[0] <= 'z')
            buf[klen++] = str[0] - 'a' + HID_KEY_A;
        if ('A' <= str[0] && str[0] <= 'Z') {
            buf[klen++] = str[0] - 'A' + HID_KEY_A;
            if (mod) ADD_SHIFT(*mod);
        }
    }
    TRYFREE(dup);
exit:
    if (klen < 6) buf[klen] = HID_KEY_NONE;
    return buf;
}

const char * keycode2str(uint8_t code, uint8_t modifier) {
    static char buf[32];
    bool shift = HAS_SHIFT(modifier);
    if (HID_KEY_A <= code && code <= HID_KEY_Z) {
        buf[0] = code - HID_KEY_A + (shift ? 'A' : 'a');
        buf[1] = '\0';
    } else if (HID_KEY_F1 <= code && code <= HID_KEY_F12) {
        snprintf(buf, sizeof(buf), "F%d", code - HID_KEY_F1 + 1);
    } else {
        LOOPN(i, LEN(keycodes_normal)) {
            if (keycodes_normal[i][0] != code) continue;
            buf[0] = keycodes_normal[i][shift ? 2 : 1];
            buf[1] = '\0';
            return buf;
        }
        LOOPN(i, LEN(keycodes_special)) {
            if (keycodes_special[i].code != code) continue;
            snprintf(buf, sizeof(buf), "<%s>", keycodes_special[i].name);
            return buf;
        }
        snprintf(buf, sizeof(buf), "<0x%02X>", code);
    }
    return buf;
}

const char * hid_modifier_str(uint8_t modifier) {
    static char buf[64];
    size_t blen = sizeof(buf), size = 0;
    LOOPN(i, 8) {
        if (modifier & (1 << i)) {
            size += snprintf(buf + size, blen - size, "%s%s",
                i ? " | " : "", modifier_names[i]);
        } else {
            buf[size] = '\0';
        }
    }
    return buf;
}

const char * hid_keycodes_str(uint8_t modifier, const uint8_t keycode[6]) {
    static char buf[64];
    size_t blen = sizeof(buf), size = 0;
    LOOPN(i, 6) {
        if (keycode[i] == HID_KEY_NONE) {
            buf[size] = '\0';
            break;
        }
        size += snprintf(buf + size, blen - size, "%s%s",
            i ? " | " : "", keycode2str(keycode[i], modifier));
    }
    return buf;
}

bool hid_report_keybd(
    hid_target_t to, uint8_t mod, const uint8_t *keycode, size_t len
) {
    hid_report_t report = {
        .id = REPORT_ID_KEYBD,
        .keybd = { .modifier = mod, .keycode = { 0 } }
    };
    memcpy(report.keybd.keycode, keycode, MIN(len, 6));
    return hid_report_send(to, &report);
}

bool hid_report_keybd_press(hid_target_t to, const char *str, uint32_t ms) {
    uint8_t modifier = str2modifier(str), klen = 0;
    const uint8_t *keycode = str2keycodes(str, &modifier);
    while (klen < 6) { if (keycode[klen] == HID_KEY_NONE) break; klen++; }
    bool sent = hid_report_keybd(to, modifier, keycode, klen);
    if (sent && (modifier || klen) && ms) {
        msleep(ms);
        sent = hid_report_keybd(to, 0, NULL, 0);
    }
    return sent;
}

void hid_handle_keybd(
    hid_target_t from, hid_keybd_report_t *rpt, hid_key_cb key_cb
) {
    if (!rpt) return;
    static uint8_t kcnum = LEN(rpt->keycode);
    static uint8_t pmods[HID_TARGET_CNT];
    static uint8_t prevs[HID_TARGET_CNT][LEN(rpt->keycode)];
    uint8_t *next = rpt->keycode, *prev = prevs[from];
    LOOPN(i, kcnum) {
        bool prev_found = false, next_found = false;
        LOOPN(j, kcnum) {
            if (prev[i] == next[j]) next_found = true;
            if (next[i] == prev[j]) prev_found = true;
        }
        if (prev[i] > HID_KEY_ERROR_UNDEFINED && !next_found) {
            if (key_cb) key_cb(prev[i], false);
            ESP_LOGI(TAG, "%s released", keycode2str(prev[i], pmods[from]));
        }
        if (next[i] > HID_KEY_ERROR_UNDEFINED && !prev_found) {
            if (key_cb) key_cb(next[i], true);
            ESP_LOGI(TAG, "%s pressed modifier %s",
                     keycode2str(next[i], rpt->modifier),
                     hid_modifier_str(rpt->modifier));
        }
    }
    memcpy(prev, next, kcnum);
    pmods[from] = rpt->modifier;
}
