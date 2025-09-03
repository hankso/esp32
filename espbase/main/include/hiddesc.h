/*
 * File: hiddesc.h
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2025/7/6 6:48:46
 * Desc: HID descriptors are the same whether through USB or BT
 */

#pragma once

#define DUAL_TUSB

/*
 * Copied from tinyusb-v0.9.0/src/class/hid/hid.h
 * And HID Usage Table for Universal Serial Device 1.21
 */

#ifndef HID_REPORT_DATA_0

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

// Table 3.1: Usage Page Summary
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
    HID_USAGE_PAGE_MONITOR         = 0x80, // 0x80 - 0x83
    HID_USAGE_PAGE_POWER           = 0x84, // 0x84 - 0x87
    HID_USAGE_PAGE_BARCODE_SCANNER = 0x8c,
    HID_USAGE_PAGE_SCALE           = 0x8d,
    HID_USAGE_PAGE_MSR             = 0x8e,
    HID_USAGE_PAGE_CAMERA          = 0x90,
    HID_USAGE_PAGE_ARCADE          = 0x91,
    HID_USAGE_PAGE_VENDOR          = 0xFF00 // 0xFF00 - 0xFFFF
};

// Table 4.1: Generic Desktop Page
enum {
    HID_USAGE_DESKTOP_POINTER                               = 0x01,
    HID_USAGE_DESKTOP_MOUSE                                 = 0x02,
    HID_USAGE_DESKTOP_JOYSTICK                              = 0x04,
    HID_USAGE_DESKTOP_GAMEPAD                               = 0x05,
    HID_USAGE_DESKTOP_KEYBOARD                              = 0x06,
    HID_USAGE_DESKTOP_KEYPAD                                = 0x07,
    HID_USAGE_DESKTOP_MULTI_AXIS_CONTROLLER                 = 0x08,
    HID_USAGE_DESKTOP_TABLET_PC_SYSTEM                      = 0x09,
    HID_USAGE_DESKTOP_WATER_COOLING                         = 0x0A,
    HID_USAGE_DESKTOP_COMPUTER_CHASSIS                      = 0x0B,
    HID_USAGE_DESKTOP_WIRELESS_RADIO                        = 0x0C,
    HID_USAGE_DESKTOP_PORTABLE_DEVICE                       = 0x0D,
    HID_USAGE_DESKTOP_SYSTEM_MULTI_AXIS                     = 0x0E,
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

// Table 15.1: Consumer Page
// Only contains controls that supported by Windows (whole list is too long)
enum {
    HID_USAGE_CONSUMER_CONTROL                           = 0x0001,
    HID_USAGE_CONSUMER_POWER                             = 0x0030,
    HID_USAGE_CONSUMER_RESET                             = 0x0031,
    HID_USAGE_CONSUMER_SLEEP                             = 0x0032,
    HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT              = 0x006F,
    HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT              = 0x0070,
    HID_USAGE_CONSUMER_WIRELESS_RADIO_CONTROLS           = 0x000C,
    HID_USAGE_CONSUMER_WIRELESS_RADIO_BUTTONS            = 0x00C6,
    HID_USAGE_CONSUMER_WIRELESS_RADIO_LED                = 0x00C7,
    HID_USAGE_CONSUMER_WIRELESS_RADIO_SLIDER_SWITCH      = 0x00C8,
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
    HID_USAGE_CONSUMER_AL_CONSUMER_CONTROL_CONFIGURATION = 0x0183,
    HID_USAGE_CONSUMER_AL_EMAIL_READER                   = 0x018A,
    HID_USAGE_CONSUMER_AL_CALCULATOR                     = 0x0192,
    HID_USAGE_CONSUMER_AL_LOCAL_BROWSER                  = 0x0194,
    HID_USAGE_CONSUMER_AC_SEARCH                         = 0x0221,
    HID_USAGE_CONSUMER_AC_HOME                           = 0x0223,
    HID_USAGE_CONSUMER_AC_BACK                           = 0x0224,
    HID_USAGE_CONSUMER_AC_FORWARD                        = 0x0225,
    HID_USAGE_CONSUMER_AC_STOP                           = 0x0226,
    HID_USAGE_CONSUMER_AC_REFRESH                        = 0x0227,
    HID_USAGE_CONSUMER_AC_BOOKMARKS                      = 0x022A,
    HID_USAGE_CONSUMER_AC_PAN                            = 0x0238,
};

// Table 16.1: Digitizer Page
enum {
    HID_USAGE_DIGITIZER_DIGITIZER               = 0x01,
    HID_USAGE_DIGITIZER_PEN                     = 0x02,
    HID_USAGE_DIGITIZER_LIGHT_PEN               = 0x03,
    HID_USAGE_DIGITIZER_TOUCH_SCREEN            = 0x04,
    HID_USAGE_DIGITIZER_TOUCH_PAD               = 0x05,
    HID_USAGE_DIGITIZER_WHITEBOARD              = 0x06,
    HID_USAGE_DIGITIZER_COORDINATE_MEASURING    = 0x07,
    HID_USAGE_DIGITIZER_3D_DIGITITER            = 0x08,
    HID_USAGE_DIGITIZER_STEREO_PLOTTER          = 0x09,
    HID_USAGE_DIGITIZER_DEVICE_CONFIGURATION    = 0x0E,
    HID_USAGE_DIGITIZER_STYLUS                  = 0x20,
    HID_USAGE_DIGITIZER_PUCK                    = 0x21,
    HID_USAGE_DIGITIZER_FINGER                  = 0x22,
    HID_USAGE_DIGITIZER_DEVICE_SETTING          = 0x23,
    HID_USAGE_DIGITIZER_CHARACTER_GESTURE       = 0x24,
    HID_USAGE_DIGITIZER_TIP_PRESSURE            = 0x30,
    HID_USAGE_DIGITIZER_IN_RANGE                = 0x32,
    HID_USAGE_DIGITIZER_TOUCH                   = 0x33,
    HID_USAGE_DIGITIZER_UNTOUCH                 = 0x34,
    HID_USAGE_DIGITIZER_TAP                     = 0x35,
    HID_USAGE_DIGITIZER_TIP_SWITCH              = 0x42,
    HID_USAGE_DIGITIZER_WIDTH                   = 0x48,
    HID_USAGE_DIGITIZER_HEIGHT                  = 0x49,
    HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER      = 0x51,
    HID_USAGE_DIGITIZER_DEVICE_MODE             = 0x52,
    HID_USAGE_DIGITIZER_DEVICE_IDENTIFIER       = 0x53,
    HID_USAGE_DIGITIZER_CONTACT_COUNT           = 0x54,
    HID_USAGE_DIGITIZER_CONTACT_COUNT_MAX       = 0x55,
    HID_USAGE_DIGITIZER_SCAN_TIME               = 0x56,
    HID_USAGE_DIGITIZER_SURFACE_SWITCH          = 0x57,
    HID_USAGE_DIGITIZER_BUTTON_SWITCH           = 0x58,
};
#endif // HID_REPORT_DATA_0

/*
 * Modified on tinyusb-v0.9.0/class/hid/hid_device.h
 */

// 69 Bytes with REPORT_ID
#define HID_REPORT_DESC_KEYBD(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP     )                          ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_KEYBOARD )                          ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION )                          ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        /* 1-byte Modifier Keys (Shfit, Control, Alt, GUI) */               \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_KEYBOARD )                         ,\
            HID_USAGE_MIN    ( 0xE0                                   )    ,\
            HID_USAGE_MAX    ( 0xE7                                   )    ,\
            HID_LOGICAL_MIN  ( 0                                      )    ,\
            HID_LOGICAL_MAX  ( 1                                      )    ,\
            HID_REPORT_COUNT ( 8                                      )    ,\
            HID_REPORT_SIZE  ( 1                                      )    ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )    ,\
        /* 1-byte Reserved (consumer report, only works on linux) */        \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_CONSUMER )                         ,\
            HID_USAGE_MIN    ( 0                                      )    ,\
            HID_USAGE_MAX    ( 255                                    )    ,\
            HID_LOGICAL_MIN  ( 0                                      )    ,\
            HID_LOGICAL_MAX  ( 255                                    )    ,\
            HID_REPORT_COUNT ( 1                                      )    ,\
            HID_REPORT_SIZE  ( 8                                      )    ,\
            HID_INPUT        ( HID_DATA | HID_ARRAY | HID_ABSOLUTE    )    ,\
        /* 6-byte Keycodes */                                               \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_KEYBOARD )                         ,\
            HID_USAGE_MIN    ( 0                                      )    ,\
            HID_USAGE_MAX    ( 255                                    )    ,\
            HID_LOGICAL_MIN  ( 0                                      )    ,\
            HID_LOGICAL_MAX  ( 255                                    )    ,\
            HID_REPORT_COUNT ( 6                                      )    ,\
            HID_REPORT_SIZE  ( 8                                      )    ,\
            HID_INPUT        ( HID_DATA | HID_ARRAY | HID_ABSOLUTE    )    ,\
        /* 5-bit LED + 3-bit custom usage */                                \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_LED )                              ,\
            HID_USAGE_MIN    ( 1                                      )    ,\
            HID_USAGE_MAX    ( 8                                      )    ,\
            HID_REPORT_COUNT ( 8                                      )    ,\
            HID_REPORT_SIZE  ( 1                                      )    ,\
            HID_OUTPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )    ,\
    HID_COLLECTION_END

// 63 Bytes with REPORT_ID
#define HID_REPORT_DESC_MOUSE(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP      )                         ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_MOUSE     )                         ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION  )                         ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_USAGE      ( HID_USAGE_DESKTOP_POINTER )                       ,\
        HID_COLLECTION ( HID_COLLECTION_PHYSICAL   )                       ,\
            /* 1-byte Left, Right, Middle, Backward, Forward buttons */     \
            HID_USAGE_PAGE ( HID_USAGE_PAGE_BUTTON )                       ,\
                HID_USAGE_MIN   ( 1                                      ) ,\
                HID_USAGE_MAX   ( 8                                      ) ,\
                HID_LOGICAL_MIN ( 0                                      ) ,\
                HID_LOGICAL_MAX ( 1                                      ) ,\
                HID_REPORT_COUNT( 8                                      ) ,\
                HID_REPORT_SIZE ( 1                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
            /* 3-byte X, Y, wheel position [-127, 127] */                   \
            HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                      ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_X                    ) ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_Y                    ) ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_WHEEL                ) ,\
                HID_LOGICAL_MIN ( 0x81                                   ) ,\
                HID_LOGICAL_MAX ( 0x7f                                   ) ,\
                HID_REPORT_COUNT( 3                                      ) ,\
                HID_REPORT_SIZE ( 8                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
            /* 1-byte Horizontal wheel scroll [-127, 127] */                \
            HID_USAGE_PAGE ( HID_USAGE_PAGE_CONSUMER )                     ,\
                HID_USAGE_N     ( HID_USAGE_CONSUMER_AC_PAN, 2           ) ,\
                HID_LOGICAL_MIN ( 0x81                                   ) ,\
                HID_LOGICAL_MAX ( 0x7f                                   ) ,\
                HID_REPORT_COUNT( 1                                      ) ,\
                HID_REPORT_SIZE ( 8                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
        HID_COLLECTION_END                                                 ,\
    HID_COLLECTION_END

// 74 Bytes with REPORT_ID
#define HID_REPORT_DESC_ABMSE(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP      )                         ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_MOUSE     )                         ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION  )                         ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_USAGE      ( HID_USAGE_DESKTOP_POINTER )                       ,\
        HID_COLLECTION ( HID_COLLECTION_PHYSICAL   )                       ,\
            /* 1-byte Left, Right, Middle, Backward, Forward buttons */     \
            HID_USAGE_PAGE ( HID_USAGE_PAGE_BUTTON )                       ,\
                HID_USAGE_MIN   ( 1                                      ) ,\
                HID_USAGE_MAX   ( 8                                      ) ,\
                HID_LOGICAL_MIN ( 0                                      ) ,\
                HID_LOGICAL_MAX ( 1                                      ) ,\
                HID_REPORT_COUNT( 8                                      ) ,\
                HID_REPORT_SIZE ( 1                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
            /* 4-byte X, Y [0, 32767] */                                    \
            HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                      ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_X                    ) ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_Y                    ) ,\
                HID_LOGICAL_MIN ( 0                                      ) ,\
                HID_LOGICAL_MAX_N( 0x7FFF, 2                             ) ,\
                HID_REPORT_COUNT( 2                                      ) ,\
                HID_REPORT_SIZE ( 16                                     ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
                /* 1-byte Vertical wheel scroll [-127, 127] */              \
                HID_USAGE       ( HID_USAGE_DESKTOP_WHEEL                ) ,\
                HID_LOGICAL_MIN ( 0x81                                   ) ,\
                HID_LOGICAL_MAX ( 0x7f                                   ) ,\
                HID_REPORT_COUNT( 1                                      ) ,\
                HID_REPORT_SIZE ( 8                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
            /* 1-byte Horizontal wheel scroll [-127, 127] */                \
            HID_USAGE_PAGE ( HID_USAGE_PAGE_CONSUMER )                     ,\
                HID_USAGE_N     ( HID_USAGE_CONSUMER_AC_PAN, 2           ) ,\
                HID_LOGICAL_MIN ( 0x81                                   ) ,\
                HID_LOGICAL_MAX ( 0x7f                                   ) ,\
                HID_REPORT_COUNT( 1                                      ) ,\
                HID_REPORT_SIZE ( 8                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
        HID_COLLECTION_END                                                 ,\
    HID_COLLECTION_END

// 50 Bytes with REPORT_ID
#define HID_REPORT_DESC_POINT(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER         )                    ,\
    HID_USAGE      ( HID_USAGE_DIGITIZER_TOUCH_SCREEN )                    ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION       )                    ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_USAGE      ( HID_USAGE_DIGITIZER_STYLUS )                      ,\
        HID_COLLECTION ( HID_COLLECTION_PHYSICAL    )                      ,\
            /* 1-byte Tip Switch, In Range */                               \
            HID_USAGE ( HID_USAGE_DIGITIZER_TIP_SWITCH )                   ,\
            HID_USAGE ( HID_USAGE_DIGITIZER_IN_RANGE   )                   ,\
                HID_LOGICAL_MIN ( 0                                      ) ,\
                HID_LOGICAL_MAX ( 1                                      ) ,\
                HID_REPORT_COUNT( 8                                      ) ,\
                HID_REPORT_SIZE ( 1                                      ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
            /* 4-byte X, Y [0%, 100%] */                                    \
            HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                      ,\
            HID_USAGE      ( HID_USAGE_DESKTOP_POINTER )                   ,\
            HID_COLLECTION ( HID_COLLECTION_PHYSICAL   )                   ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_X                    ) ,\
                HID_USAGE       ( HID_USAGE_DESKTOP_Y                    ) ,\
                HID_LOGICAL_MIN ( 0                                      ) ,\
                HID_LOGICAL_MAX_N( 10000, 2                              ) ,\
                HID_REPORT_COUNT( 2                                      ) ,\
                HID_REPORT_SIZE ( 16                                     ) ,\
                HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
            HID_COLLECTION_END                                             ,\
        HID_COLLECTION_END                                                 ,\
    HID_COLLECTION_END

// TODO: fix 10-fingers touch hid desc
#define HID_REPORT_DESC_TOUCH(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER         )                    ,\
    HID_USAGE      ( HID_USAGE_DIGITIZER_TOUCH_SCREEN )                    ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION       )                    ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER   )                      ,\
        HID_USAGE      ( HID_USAGE_DIGITIZER_FINGER )                      ,\
        HID_COLLECTION ( HID_COLLECTION_LOGICAL     )                      ,\
            /* 1-byte Tip Switch, Contact Indentifier */                    \
            HID_USAGE       ( HID_USAGE_DIGITIZER_TIP_SWITCH         )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX ( 1                                      )     ,\
            HID_REPORT_COUNT( 4                                      )     ,\
            HID_REPORT_SIZE ( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            HID_USAGE       ( HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER )     ,\
            HID_LOGICAL_MAX ( 15                                     )     ,\
            HID_REPORT_SIZE ( 4                                      )     ,\
            HID_REPORT_COUNT( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            /* 4-byte X, Y [0%, 100%] */                                    \
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP                 )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_X                    )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_Y                    )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX_N( 10000, 2                              )     ,\
            HID_REPORT_COUNT( 2                                      )     ,\
            HID_REPORT_SIZE ( 16                                     )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
        HID_COLLECTION_END                                                 ,\
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER   )                      ,\
        HID_USAGE      ( HID_USAGE_DIGITIZER_FINGER )                      ,\
        HID_COLLECTION ( HID_COLLECTION_LOGICAL     )                      ,\
            /* 1-byte Tip Switch, Contact Indentifier */                    \
            HID_USAGE       ( HID_USAGE_DIGITIZER_TIP_SWITCH         )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX ( 1                                      )     ,\
            HID_REPORT_COUNT( 4                                      )     ,\
            HID_REPORT_SIZE ( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            HID_USAGE       ( HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER )     ,\
            HID_LOGICAL_MAX ( 15                                     )     ,\
            HID_REPORT_SIZE ( 4                                      )     ,\
            HID_REPORT_COUNT( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            /* 4-byte X, Y [0%, 100%] */                                    \
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP                 )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_X                    )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_Y                    )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX_N( 10000, 2                              )     ,\
            HID_REPORT_COUNT( 2                                      )     ,\
            HID_REPORT_SIZE ( 16                                     )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
        HID_COLLECTION_END                                                 ,\
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER   )                      ,\
        HID_USAGE      ( HID_USAGE_DIGITIZER_FINGER )                      ,\
        HID_COLLECTION ( HID_COLLECTION_LOGICAL     )                      ,\
            /* 1-byte Tip Switch, Contact Indentifier */                    \
            HID_USAGE       ( HID_USAGE_DIGITIZER_TIP_SWITCH         )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX ( 1                                      )     ,\
            HID_REPORT_COUNT( 4                                      )     ,\
            HID_REPORT_SIZE ( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            HID_USAGE       ( HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER )     ,\
            HID_LOGICAL_MAX ( 15                                     )     ,\
            HID_REPORT_SIZE ( 4                                      )     ,\
            HID_REPORT_COUNT( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            /* 4-byte X, Y [0%, 100%] */                                    \
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP                 )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_X                    )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_Y                    )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX_N( 10000, 2                              )     ,\
            HID_REPORT_COUNT( 2                                      )     ,\
            HID_REPORT_SIZE ( 16                                     )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
        HID_COLLECTION_END                                                 ,\
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER   )                      ,\
        HID_USAGE      ( HID_USAGE_DIGITIZER_FINGER )                      ,\
        HID_COLLECTION ( HID_COLLECTION_LOGICAL     )                      ,\
            /* 1-byte Tip Switch, Contact Indentifier */                    \
            HID_USAGE       ( HID_USAGE_DIGITIZER_TIP_SWITCH         )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX ( 1                                      )     ,\
            HID_REPORT_COUNT( 4                                      )     ,\
            HID_REPORT_SIZE ( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            HID_USAGE       ( HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER )     ,\
            HID_LOGICAL_MAX ( 15                                     )     ,\
            HID_REPORT_SIZE ( 4                                      )     ,\
            HID_REPORT_COUNT( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            /* 4-byte X, Y [0%, 100%] */                                    \
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP                 )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_X                    )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_Y                    )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX_N( 10000, 2                              )     ,\
            HID_REPORT_COUNT( 2                                      )     ,\
            HID_REPORT_SIZE ( 16                                     )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
        HID_COLLECTION_END                                                 ,\
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER   )                      ,\
        HID_USAGE      ( HID_USAGE_DIGITIZER_FINGER )                      ,\
        HID_COLLECTION ( HID_COLLECTION_LOGICAL     )                      ,\
            /* 1-byte Tip Switch, Contact Indentifier */                    \
            HID_USAGE       ( HID_USAGE_DIGITIZER_TIP_SWITCH         )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX ( 1                                      )     ,\
            HID_REPORT_COUNT( 4                                      )     ,\
            HID_REPORT_SIZE ( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            HID_USAGE       ( HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER )     ,\
            HID_LOGICAL_MAX ( 15                                     )     ,\
            HID_REPORT_SIZE ( 4                                      )     ,\
            HID_REPORT_COUNT( 1                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            /* 4-byte X, Y [0%, 100%] */                                    \
            HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP                 )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_X                    )     ,\
            HID_USAGE       ( HID_USAGE_DESKTOP_Y                    )     ,\
            HID_LOGICAL_MIN ( 0                                      )     ,\
            HID_LOGICAL_MAX_N( 10000, 2                              )     ,\
            HID_REPORT_COUNT( 2                                      )     ,\
            HID_REPORT_SIZE ( 16                                     )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
        HID_COLLECTION_END                                                 ,\
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER   )                      ,\
            /* 2-byte Scan time [0, 65545] */                               \
            HID_USAGE       ( HID_USAGE_DIGITIZER_SCAN_TIME          )     ,\
            HID_LOGICAL_MAX_N( 0xFFFF, 2                             )     ,\
            HID_REPORT_COUNT( 1                                      )     ,\
            HID_REPORT_SIZE ( 16                                     )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
            /* 1-byte Contact count */                                      \
            HID_USAGE       ( HID_USAGE_DIGITIZER_CONTACT_COUNT      )     ,\
            HID_LOGICAL_MAX ( 127                                    )     ,\
            HID_REPORT_COUNT( 1                                      )     ,\
            HID_REPORT_SIZE ( 8                                      )     ,\
            HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )     ,\
    HID_COLLECTION_END

// 60 Bytes with REPORT_ID
#define HID_REPORT_DESC_GMPAD(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP     )                          ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_GAMEPAD  )                          ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION )                          ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        /* 6-byte LX, LY, LZ, RX, RY, RZ [-127, 127] */                     \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                          ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_X                    )    ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_Y                    )    ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_Z                    )    ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_RX                   )    ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_RY                   )    ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_RZ                   )    ,\
            HID_LOGICAL_MIN  ( 0x81                                   )    ,\
            HID_LOGICAL_MAX  ( 0x7f                                   )    ,\
            HID_REPORT_COUNT ( 6                                      )    ,\
            HID_REPORT_SIZE  ( 8                                      )    ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )    ,\
        /* 8-bit DPad/Hat */                                                \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP )                          ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_HAT_SWITCH           )    ,\
            HID_USAGE        ( HID_USAGE_DESKTOP_HAT_SWITCH           )    ,\
            HID_LOGICAL_MIN  ( 1                                      )    ,\
            HID_LOGICAL_MAX  ( 8                                      )    ,\
            HID_PHYSICAL_MIN ( 0                                      )    ,\
            HID_PHYSICAL_MAX_N( 315, 2                                )    ,\
            HID_REPORT_COUNT ( 2                                      )    ,\
            HID_REPORT_SIZE  ( 4                                      )    ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )    ,\
        /* 16-bit buttons */                                                \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_BUTTON )                           ,\
            HID_USAGE_MIN    ( 1                                      )    ,\
            HID_USAGE_MAX    ( 16                                     )    ,\
            HID_LOGICAL_MIN  ( 0                                      )    ,\
            HID_LOGICAL_MAX  ( 1                                      )    ,\
            HID_REPORT_COUNT ( 16                                     )    ,\
            HID_REPORT_SIZE  ( 1                                      )    ,\
            HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )    ,\
    HID_COLLECTION_END

// 23 Bytes with REPORT_ID
#define HID_REPORT_DESC_SCTRL(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP           )                    ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_SYSTEM_CONTROL )                    ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION       )                    ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_LOGICAL_MIN  ( 0                                   )           ,\
        HID_LOGICAL_MAX  ( 255                                 )           ,\
        HID_USAGE_MIN    ( 0                                   )           ,\
        HID_USAGE_MAX    ( 255                                 )           ,\
        HID_REPORT_COUNT ( 1                                   )           ,\
        HID_REPORT_SIZE  ( 8                                   )           ,\
        HID_INPUT        ( HID_DATA | HID_ARRAY | HID_ABSOLUTE )           ,\
    HID_COLLECTION_END

// 56 Bytes with REPORT_ID
#define HID_REPORT_DESC_SDIAL(...)                                          \
    HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP              )                 ,\
    HID_USAGE      ( HID_USAGE_DESKTOP_SYSTEM_MULTI_AXIS )                 ,\
    HID_COLLECTION ( HID_COLLECTION_APPLICATION          )                 ,\
        __VA_ARGS__ /* Report ID if any */                                  \
        HID_USAGE_PAGE ( HID_USAGE_PAGE_DIGITIZER )                        ,\
        HID_USAGE      ( HID_USAGE_DIGITIZER_PUCK )                        ,\
        HID_COLLECTION ( HID_COLLECTION_PHYSICAL  )                        ,\
            /* 1-bit SDIAL_U / SDIAL_D */                                   \
            HID_USAGE_PAGE     ( HID_USAGE_PAGE_BUTTON                  )  ,\
            HID_USAGE          ( 1                                      )  ,\
            HID_REPORT_COUNT   ( 1                                      )  ,\
            HID_REPORT_SIZE    ( 1                                      )  ,\
            HID_LOGICAL_MIN    ( 0                                      )  ,\
            HID_LOGICAL_MAX    ( 1                                      )  ,\
            HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE )  ,\
            /* 15-bit SDIAL_L / SDIAL_R */                                  \
            HID_USAGE_PAGE     ( HID_USAGE_PAGE_DESKTOP                 )  ,\
            HID_USAGE          ( HID_USAGE_DESKTOP_DIAL                 )  ,\
            HID_REPORT_COUNT   ( 1                                      )  ,\
            HID_REPORT_SIZE    ( 15                                     )  ,\
            HID_UNIT_EXPONENT  ( 0x0F                                   )  ,\
            HID_UNIT           ( 0x14        /* Eng Rot - Ang Pos */    )  ,\
            HID_PHYSICAL_MIN_N ( -3600, 2                               )  ,\
            HID_PHYSICAL_MAX_N (  3600, 2                               )  ,\
            HID_LOGICAL_MIN_N  ( -3600, 2                               )  ,\
            HID_LOGICAL_MAX_N  (  3600, 2                               )  ,\
            HID_INPUT          ( HID_DATA | HID_VARIABLE | HID_RELATIVE )  ,\
        HID_COLLECTION_END                                                 ,\
    HID_COLLECTION_END
