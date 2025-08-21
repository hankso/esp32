/*
 * File: hidtool.c
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/17 12:56:20
 */

#include "hiddesc.h"
#include "hidtool.h"
#include "network.h"            // for network_parse_addr
#include "filesys.h"            // for filesys_load
#include "usbmode.h"            // for hidu_send_report
#include "btmode.h"             // for hidb_send_report
#include "screen.h"             // for scn_command
#include "config.h"

#include "lwip/sockets.h"

static const char *TAG = "HIDTool";

#ifdef CONFIG_BASE_USE_WIFI
static struct {
    int sock;
    struct sockaddr_storage addr;
} uctx;
#endif

static hid_gmpad_data_t gctx;   // defined here for logging

hidtool_t HIDTool = {
    .pad = 0, .rlen = {
        [0]               = 0,
        [REPORT_ID_KEYBD] = SIZEOF(hid_report_t, keybd),
        [REPORT_ID_MOUSE] = SIZEOF(hid_report_t, mouse),
        [REPORT_ID_ABMSE] = SIZEOF(hid_report_t, abmse),
        [REPORT_ID_TOUCH] = SIZEOF(hid_report_t, touch),
        [REPORT_ID_GMPAD] = SIZEOF(hid_report_t, gmpad),
        [REPORT_ID_SCTRL] = SIZEOF(hid_report_t, sctrl),
        [REPORT_ID_SDIAL] = SIZEOF(hid_report_t, sdial),
    },
    .desc = { 0 }, .dlen = 0,
    .vid = 0xCAFE, .pid = 0x4000, .ver = 0,
    .dstr = "", .vendor = "", .serial = "",
};

void hidtool_initialize() {
    if (!strlen(Config.app.HID_MODE)) return;

    int vals[2];
    if (parse_all(Config.info.VER, vals, 2) == 2)
        HIDTool.ver = ((vals[0] & 0xFF) << 8) | (vals[1] & 0xFF);
#ifdef CONFIG_TINYUSB_DESC_MANUFACTURER_STRING
    HIDTool.vendor = CONFIG_TINYUSB_DESC_MANUFACTURER_STRING;
#else
    HIDTool.vendor = Config.info.NAME;
#endif
    HIDTool.serial = Config.info.UID;

    const char *hidfile = NULL;
    if (!strcasecmp(Config.app.HID_MODE, "GENERAL")) {
        HIDTool.vid = 0x16C0;   // libusb debug vendor id
        HIDTool.vid = 0x05DF;
        HIDTool.pad = GMPAD_GENERAL;
        HIDTool.dstr = "Keybd(1), Mouse(2-4), Joyst(5), SCtrl(6), SDial(7)";
        HIDTool.rlen[REPORT_ID_GMPAD] = SIZEOF(hid_gmpad_report_t, general);
        uint8_t desc[] = {
            HID_REPORT_DESC_KEYBD(HID_REPORT_ID(REPORT_ID_KEYBD)),  // 69 Bytes
            HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),  // 63 Bytes
            HID_REPORT_DESC_ABMSE(HID_REPORT_ID(REPORT_ID_ABMSE)),  // 74 Bytes
            HID_REPORT_DESC_GMPAD(HID_REPORT_ID(REPORT_ID_GMPAD)),  // 70 Bytes
            HID_REPORT_DESC_SCTRL(HID_REPORT_ID(REPORT_ID_SCTRL)),  // 23 Bytes
            HID_REPORT_DESC_SDIAL(HID_REPORT_ID(REPORT_ID_SDIAL)),  // 56 Bytes
        };
        if (sizeof(desc) < sizeof(HIDTool.desc))
            memcpy(HIDTool.desc, desc, HIDTool.dlen = sizeof(desc));
    } else if (!strcasecmp(Config.app.HID_MODE, "XINPUT")) {
        HIDTool.vid = 0x045E;
        HIDTool.pid = 0x0B13;
        HIDTool.ver = 0x0509;
        HIDTool.pad = GMPAD_XINPUT;
        HIDTool.dstr = "Microsoft XInput compatible gamepad";
        HIDTool.rlen[REPORT_ID_GMPAD] = SIZEOF(hid_gmpad_report_t, xinput);
        hidfile = "xinput.hid";                                     // 283 Bytes
    } else if (!strcasecmp(Config.app.HID_MODE, "SWITCH")) {
        HIDTool.vid = 0x057E;
        HIDTool.pid = 0x2009;
        HIDTool.ver = 0x0101;
        HIDTool.pad = GMPAD_SWITCH;
        HIDTool.dstr = "Mintendo wireless gamepad";
        HIDTool.rlen[REPORT_ID_GMPAD] = SIZEOF(hid_gmpad_report_t, nswitch);
        hidfile = "switch.hid";                                     // 170 Bytes
    } else if (!strcasecmp(Config.app.HID_MODE, "DSENSE")) {
        HIDTool.vid = 0x054C;
        HIDTool.pid = 0x0CE6;
        HIDTool.ver = 0x0101;
        HIDTool.pad = GMPAD_DSENSE;
        HIDTool.dstr = "PlayStation DualSense gamepad";
        HIDTool.rlen[REPORT_ID_GMPAD] = SIZEOF(hid_gmpad_report_t, dsense);
        hidfile = "dsense.hid";                                     // 279 Bytes
    } else {
        ESP_LOGE(TAG, "Unknown HID MODE: %s", Config.app.HID_MODE);
        return;
    }
    if (hidfile) {
        size_t dlen = sizeof(HIDTool.desc);     // skip files larger than buffer
        const char *hidpath = fjoin(2, Config.sys.DIR_DATA, hidfile);
        uint8_t *desc = fload(hidpath, &dlen);
        if (desc) memcpy(HIDTool.desc, desc, HIDTool.dlen = dlen);
        TRYFREE(desc);
    }
#ifdef CONFIG_BASE_USE_WIFI
    if (!network_parse_addr(Config.app.HID_HOST, 4950, &uctx.addr)) {
        if (uctx.addr.ss_family == AF_INET) {
            uctx.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        } else {
            uctx.sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_IPV6);
        }
    }
#endif
}

bool hid_report_send(hid_target_t to, hid_report_t *rpt) {
    bool sent = false;
    if (!rpt || !rpt->id || rpt->id >= REPORT_ID_MAX) {
        if (rpt) ESP_LOGW(TAG, "Unknown report id: %d", rpt->id);
        return sent;
    }
    if (!HIDTool.pad) {
        if (rpt->id == REPORT_ID_GMPAD) return sent;
    } else if (HIDTool.pad != GMPAD_GENERAL) {
        if (rpt->id != REPORT_ID_GMPAD) return sent;
    }
    if (!rpt->size) rpt->size = HIDTool.rlen[rpt->id];
#ifdef CONFIG_BASE_USB_HID_DEVICE
    if (to | HID_TARGET_USB) sent |= hidu_send_report(rpt);
#endif
#ifdef CONFIG_BASE_USE_BT
    if (to | HID_TARGET_BLE) sent |= hidb_send_report(rpt);
#endif
#ifdef CONFIG_BASE_USE_WIFI
    if (to | HID_TARGET_UDP)
        sent |= uctx.sock && sendto(
            uctx.sock, (void *)rpt, rpt->size, 0,
            (struct sockaddr *)&uctx.addr, sizeof(uctx.addr)
        ) >= 0;
#endif
#ifdef CONFIG_BASE_USE_SCN
    if (to | HID_TARGET_SCN) sent |= scn_command(SCN_INP, rpt) == ESP_OK;
#endif
    if (to | HID_TARGET_SCN || !sent) {
        // do nothing
    } else if (rpt->id == REPORT_ID_KEYBD) {
        uint8_t mod = rpt->keybd.modifier;
        ESP_LOGI(TAG, "KEYBD MOD 0x%02X KEY %s",
                mod, hid_keycodes_str(rpt->keybd.keycode, mod));
    } else if (rpt->id == REPORT_ID_MOUSE) {
        ESP_LOGI(TAG, "MOUSE X %4d Y %4d V %3d H %3d BTN %s",
                rpt->mouse.x, rpt->mouse.y, rpt->mouse.wheel, rpt->mouse.pan,
                hid_btncode_str(rpt->mouse.buttons));
    } else if (rpt->id == REPORT_ID_ABMSE) {
        ESP_LOGI(TAG, "ABMSE X %5u Y %5u V %3d H %3d BTN %s",
                rpt->abmse.x, rpt->abmse.y, rpt->abmse.wheel, rpt->abmse.pan,
                hid_btncode_str(rpt->abmse.buttons));
    } else if (rpt->id == REPORT_ID_GMPAD) {
        ESP_LOGI(TAG, "GMPAD L %4d %-4d R %4d %-4d H %02X T %02X%02X BTN %s",
                gctx.lx >> 8, gctx.ly >> 8, gctx.rx >> 8, gctx.ry >> 8,
                gctx.dpad, gctx.lt, gctx.rt, format_binary(gctx.btns, 12));
    } else if (rpt->id == REPORT_ID_SCTRL) {
        ESP_LOGI(TAG, "SCTRL 0x%02X", rpt->sctrl);
    } else if (rpt->id == REPORT_ID_SDIAL) {
        ESP_LOGI(TAG, "SDIAL 0x%04X", *(uint16_t *)rpt->sdial);
    }
    return sent;
}

/******************************************************************************
 * HID type: Surface Dial
 */

bool hid_report_sdial(hid_target_t to, hid_sdial_keycode_t k) {
    hid_report_t report = {
        .id = REPORT_ID_SDIAL,
        .sdial = { k, (k == SDIAL_L) ? 0xFF : 0 }
    };
    return hid_report_send(to, &report);
}

bool hid_report_sdial_click(hid_target_t to, uint32_t ms) {
    bool sent = hid_report_sdial(to, SDIAL_D);
    if (sent && ms != UINT32_MAX) {
        msleep(ms);
        sent = hid_report_sdial(to, SDIAL_U);
    }
    return sent;
}

/******************************************************************************
 * HID type: System Control
 */

bool hid_report_sctrl(hid_target_t to, hid_sctrl_keycode_t k) {
    hid_report_t report = { .id = REPORT_ID_SCTRL, .sctrl = k | 0x80 };
    bool sent = hid_report_send(to, &report);
    if (sent) {
        msleep(50);
        report.sctrl = 0;
        sent = hid_report_send(to, &report);
    }
    return sent;
}

/******************************************************************************
 * HID type: Game Pad
 */

static hid_report_t * gmpad_dump_data(hid_report_t *report) {
    // bits  : 15  14  13  12   11    10     9    8     7  6  5  4  3 2 1 0
    // gctx  : L   D   R   U    Share Home   Next Prev  RS LS RB LB Y X B A
    // xinput: -   RS  LS  XBox Start Select -    -     RB LB -  Y  X - B A
    // switch: -   -   Cap Home RS    LS     Plus Minus ZR ZL R  L  Y X B A
    // dsense: -   -   Pad Home R3    L3     Opt  Share R2 L2 R1 L1 T C C S
    hid_gmpad_report_t *rpt = &report->gmpad;
    switch (HIDTool.pad) {
    case GMPAD_GENERAL:
        rpt->general.lx = gctx.lx * 0x7F / 0x7FFF;
        rpt->general.ly = gctx.ly * 0x7F / 0x7FFF;
        rpt->general.rx = gctx.rx * 0x7F / 0x7FFF;
        rpt->general.ry = gctx.ry * 0x7F / 0x7FFF;
        rpt->general.lz = gctx.lt - 0x80;
        rpt->general.rz = gctx.rt - 0x80;
        rpt->general.dpad = gctx.dpad;
        rpt->general.btns = gctx.btns;
        break;
    case GMPAD_XINPUT:
        rpt->xinput.lx = gctx.lx + 0x8000;
        rpt->xinput.ly = gctx.ly + 0x8000;
        rpt->xinput.rx = gctx.rx + 0x8000;
        rpt->xinput.ry = gctx.ry + 0x8000;
        rpt->xinput.lt = gctx.lt << 2;
        rpt->xinput.rt = gctx.rt << 2;
        rpt->xinput.dpad = gctx.dpad & 0xF;
        rpt->xinput.btns = (
            (gctx.btns & 0x03)                  |   // A, B
            bitnread(gctx.btns, 2, 2) << 3      |   // X, Y
            bitnread(gctx.btns, 4, 2) << 6      |   // LB, RB
            bitnread(gctx.btns, 8, 3) << 10     |   // Select, Start, XBox
            bitnread(gctx.btns, 6, 2) << 13         // LS, RS
        );
        rpt->xinput.share = (gctx.btns & GMPAD_BUTTON_SHARE) ? BIT0 : 0;
        break;
    case GMPAD_SWITCH:
        rpt->nswitch.lx = gctx.lx + 0x8000;
        rpt->nswitch.ly = gctx.ly + 0x8000;
        rpt->nswitch.rx = gctx.rx + 0x8000;
        rpt->nswitch.ry = gctx.ry + 0x8000;
        rpt->nswitch.dpad = gctx.dpad & 0xF;
        rpt->nswitch.btns = (
            (gctx.btns & 0x33F)                 |   // A, B, X, Y, L, R, -, +
            (gctx.lt ? BIT6 : 0)                |   // ZL
            (gctx.rt ? BIT7 : 0)                |   // ZR
            bitnread(gctx.btns, 6, 2) << 10     |   // LS, RS
            bitnread(gctx.btns, 10, 2) << 12        // Home, Capture
        );
        break;
    case GMPAD_DSENSE:
        rpt->dsense.lx = gctx.lx * 0x7F / 0x7FFF;
        rpt->dsense.ly = gctx.ly * 0x7F / 0x7FFF;
        rpt->dsense.rx = gctx.rx * 0x7F / 0x7FFF;
        rpt->dsense.ry = gctx.ry * 0x7F / 0x7FFF;
        rpt->dsense.lt = gctx.lt;
        rpt->dsense.rt = gctx.rt;
        rpt->dsense.btns = (
            (bitread(gctx.btns, 2) << 0)        |   // Square
            (bitread(gctx.btns, 0) << 1)        |   // Cross
            (bitread(gctx.btns, 1) << 2)        |   // Circle
            (bitread(gctx.btns, 3) << 3)        |   // Triangle
            (gctx.btns & 0x330)                 |   // L1, R1, Share, Option
            (gctx.lt ? BIT6 : 0)                |   // L2
            (gctx.rt ? BIT7 : 0)                |   // R2
            bitnread(gctx.btns, 6, 2) << 10     |   // L3, R3
            bitnread(gctx.btns, 10, 2) << 12        // Home, Pad
        );
        rpt->dsense.dpad = (gctx.dpad & 0xF) | (rpt->dsense.btns << 4);
        rpt->dsense.btns >>= 4;
        break;
    }
    return report;
}

static const uint16_t dpad_map[] = {
    [GMPAD_DPAD_U]  = GMPAD_BUTTON_U,
    [GMPAD_DPAD_UR] = GMPAD_BUTTON_U | GMPAD_BUTTON_R,
    [GMPAD_DPAD_R]  = GMPAD_BUTTON_R,
    [GMPAD_DPAD_DR] = GMPAD_BUTTON_D | GMPAD_BUTTON_R,
    [GMPAD_DPAD_D]  = GMPAD_BUTTON_D,
    [GMPAD_DPAD_DL] = GMPAD_BUTTON_D | GMPAD_BUTTON_L,
    [GMPAD_DPAD_L]  = GMPAD_BUTTON_L,
    [GMPAD_DPAD_UL] = GMPAD_BUTTON_U | GMPAD_BUTTON_L,
};

static uint16_t dir2bits(hid_gmpad_dpad_t dir) {
    return dir < GMPAD_DPAD_MAX ? dpad_map[dir] : 0;
}

static hid_gmpad_dpad_t bits2dir(uint16_t bit) {
    for (uint8_t dir = GMPAD_DPAD_NONE; dir < GMPAD_DPAD_MAX; dir++) {
        if (dpad_map[dir] == (bit & 0xF000)) return dir; // bit 12-15
    }
    return GMPAD_DPAD_NONE;
}

bool hid_report_gmpad_dpad(
    hid_target_t to, hid_gmpad_dpad_t dpad1, hid_gmpad_dpad_t dpad2
) {
    hid_report_t report = { .id = REPORT_ID_GMPAD };
    gctx.btns = dir2bits(dpad1) | (gctx.btns & 0xFFF);
    gctx.dpad = (dpad1 & 0xF) | ((dpad2 & 0xF) << 4);
    return hid_report_send(to, gmpad_dump_data(&report));
}

bool hid_report_gmpad_trig(hid_target_t to, uint8_t lt, uint8_t rt) {
    hid_report_t report = { .id = REPORT_ID_GMPAD };
    gctx.lt = lt; gctx.rt = rt;
    return hid_report_send(to, gmpad_dump_data(&report));
}

bool hid_report_gmpad_joyst(
    hid_target_t to, int16_t lx, int16_t ly, int16_t rx, int16_t ry
) {
    hid_report_t report = { .id = REPORT_ID_GMPAD };
    gctx.lx = lx; gctx.ly = ly; gctx.rx = rx; gctx.ry = ry;
    return hid_report_send(to, gmpad_dump_data(&report));
}

bool hid_report_gmpad_click(hid_target_t to, const char *str, uint32_t ms) {
    bool sent = false;
    int idx = stridx(
        str, "A|B|X|Y|LB|RB|LS|RS|PREV|NEXT|HOME|SHARE|U|R|D|L|UR|DR|DL|UL");
    if (idx < 0) return sent;
    if (idx < 16) {
        sent = hid_report_gmpad_btn_add(to, BIT(idx));
    } else if (idx < 20) {  // index 16~19 -> hid_gmpad_dpad_t 2,4,6,8
        sent = hid_report_gmpad_dpad(to, 2 * idx - 30, 2 * idx - 30);
    }
    if (sent && ms != UINT32_MAX) {
        msleep(ms);
        if (idx < 16) {
            sent = hid_report_gmpad_btn_del(to, BIT(idx));
        } else if (idx < 20) {
            sent = hid_report_gmpad_dpad(to, 0, 0);
        }
    }
    return sent;
}

bool hid_report_gmpad_button(hid_target_t to, uint16_t btn, uint8_t action) {
    uint16_t val = gctx.btns;
    if (action == 3) {
        val = btn;
    } else LOOPN(i, 16) {
        if (!bitread(btn, i)) continue;
        if (bitread(val, i)) {
            if (action == 0 || action == 2) val &= ~BIT(i);
        } else {
            if (action == 1 || action == 2) val |= BIT(i);
        }
    }
    if (val == gctx.btns) return true;
    hid_report_t report = { .id = REPORT_ID_GMPAD };
    gctx.btns = val;
    gctx.dpad = bits2dir(val) | (gctx.dpad & 0xF0);
    return hid_report_send(to, gmpad_dump_data(&report));
}

/******************************************************************************
 * HID type: Mouse move / click / scroll
 */

static const char * BUTTON_STR[] = {
    "Left", "Right", "Middle", "Backward", "Forward"
};

static uint8_t str2btncode(const char *str) {
    LOOPN(i, LEN(BUTTON_STR)) {
        if (!strcasecmp(str, BUTTON_STR[i])) return 1 << i;
    }
    return 0;
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
    if (sent && btncode && ms != UINT32_MAX) {
        msleep(ms);
        sent = hid_report_mouse_button(to, 0);
    }
    return sent;
}

void hid_handle_mouse(
    hid_target_t from, hid_mouse_report_t *rpt,
    hid_key_cb key_cb, hid_pos_cb pos_cb
) {
    int idx = -1;
    LOOPN(i, 8) { if (from & BIT(i)) idx = idx == -1 ? i : -2; }
    if (idx < 0 || !rpt) return;
    static int xs[8], ys[8], btns[8];
    xs[idx] += rpt->x;
    ys[idx] += rpt->y;
    if (pos_cb) pos_cb(xs[idx], ys[idx], rpt->x, rpt->y);
    if (key_cb) LOOPN(i, 5) {
        uint8_t btn = BIT(i);
        if ((rpt->buttons & btn) == (btns[idx] & btn)) continue;
        key_cb(btn, rpt->buttons & btn);
    }
    btns[idx] = rpt->buttons;
    ESP_LOGI(TAG, "X: %5d Y: %5d V: %3d H %3d |%c|%c|%c|",
             xs[idx], ys[idx], rpt->wheel, rpt->pan,
             btns[idx] & MOUSE_BUTTON_LEFT   ? 'L' : ' ',
             btns[idx] & MOUSE_BUTTON_MIDDLE ? 'M' : ' ',
             btns[idx] & MOUSE_BUTTON_RIGHT  ? 'R' : ' ');
}

bool hid_report_mouse_moveto(hid_target_t to, uint16_t x, uint16_t y) {
    hid_report_t report = {
        .id = REPORT_ID_ABMSE,
        .abmse = { 0, x, y, 0, 0 }
    };
    return hid_report_send(to, &report);
}

void hid_handle_abmse(
    hid_target_t from, hid_abmse_report_t *rpt,
    hid_key_cb key_cb, hid_pos_cb pos_cb
) {
    int idx = -1;
    LOOPN(i, 8) { if (from & BIT(i)) idx = idx == -1 ? i : -2; }
    if (idx < 0 || !rpt) return;
    static int xs[8], ys[8], btns[8];
    int dx = rpt->x - (xs[idx] ?: rpt->x);
    int dy = rpt->y - (ys[idx] ?: rpt->y);
    if (pos_cb) pos_cb(rpt->x, rpt->y, dx, dy);
    if (key_cb) LOOPN(i, 5) {
        uint8_t btn = BIT(i);
        if ((rpt->buttons & btn) == (btns[idx] & btn)) continue;
        key_cb(btn, rpt->buttons & btn);
    }
    xs[idx] = rpt->x;
    ys[idx] = rpt->y;
    btns[idx] = rpt->buttons;
    ESP_LOGI(TAG, "X: %5d Y: %5d V: %3d H %3d |%c|%c|%c|",
             xs[idx], ys[idx], rpt->wheel, rpt->pan,
             btns[idx] & MOUSE_BUTTON_LEFT   ? 'L' : ' ',
             btns[idx] & MOUSE_BUTTON_MIDDLE ? 'M' : ' ',
             btns[idx] & MOUSE_BUTTON_RIGHT  ? 'R' : ' ');
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
    { HID_KEY_ENTER,        "Enter" },      // 10 \n or 13 \r
    { HID_KEY_ARROW_UP,     "Up" },         // 17
    { HID_KEY_ARROW_DOWN,   "Down" },       // 18
    { HID_KEY_ARROW_RIGHT,  "Right" },      // 19
    { HID_KEY_ARROW_LEFT,   "Left" },       // 20
    { HID_KEY_CANCEL,       "Cancel" },     // 24
    { HID_KEY_ESCAPE,       "Escape" },     // 27
    { HID_KEY_SPACE,        "Space" },      // 32 (should not be "special")
    { HID_KEY_DELETE,       "Delete" },     // 127
    { HID_KEY_CAPS_LOCK,    "CapsLock" },
    { HID_KEY_SCROLL_LOCK,  "ScrLock" },
    { HID_KEY_NUM_LOCK,     "NumLock" },
    { HID_KEY_PRINT_SCREEN, "PrtScn" },
    { HID_KEY_PAUSE,        "Pause" },
    { HID_KEY_MUTE,         "VolumeMute" },
    { HID_KEY_VOLUME_DOWN,  "VolumeDown" },
    { HID_KEY_VOLUME_UP,    "VolumeUp" },
    { HID_KEY_HOME,         "Home" },
    { HID_KEY_END,          "End" },
    { HID_KEY_PAGE_UP,      "PageUp" },
    { HID_KEY_PAGE_DOWN,    "PageDown" },
    { HID_KEY_INSERT,       "Insert" },
    { HID_KEY_MENU,         "Menu" },
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
    "L-Ctrl", "L-Shift", "L-Alt", "L-Meta",
    "R-Ctrl", "R-Shift", "R-Alt", "R-Meta",
      "Ctrl",   "Shift",   "Alt",   "Meta",
};

static uint8_t str2modifier(const char *str) {
    uint8_t mod = 0;
    LOOPN(i, LEN(modifier_names)) {
        if (strcasestr(str, modifier_names[i])) {
            mod |= 1 << (i % 8);
        }
    }
    return mod;
}

static const uint8_t * str2keycodes(const char *str, uint8_t *mod) {
    static uint8_t buf[6];
    size_t len = strlen(str ?: ""), klen = 0, blen = sizeof(buf);
    if (!len) goto exit;
    if (str[0] == '|') {
        buf[klen++] = HID_KEY_BACKSLASH;
        if (mod) *mod = KEYBD_MOD_ADD_SHIFT(*mod);
    }
    char *dup = strdup(str);
    for (str = strtok(dup, "|"); str && klen < blen; str = strtok(NULL, "|")) {
        if (str2modifier(str)) continue;
        uint8_t fkey;
        bool has_fkey = parse_u8(str + 1, &fkey) && fkey < 13;
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
                if (mod) *mod = KEYBD_MOD_ADD_SHIFT(*mod);
            }
        }
        if ('a' <= str[0] && str[0] <= 'z')
            buf[klen++] = str[0] - 'a' + HID_KEY_A;
        if ('A' <= str[0] && str[0] <= 'Z') {
            buf[klen++] = str[0] - 'A' + HID_KEY_A;
            if (mod) *mod = KEYBD_MOD_ADD_SHIFT(*mod);
        }
    }
    TRYFREE(dup);
exit:
    if (klen < 6) buf[klen] = HID_KEY_NONE;
    return buf;
}

const char * hid_keycode_str(uint8_t code, uint8_t modifier) {
    static char buf[32];
    bool shift = KEYBD_MOD_HAS_SHIFT(modifier);
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

const char * hid_keycodes_str(const uint8_t keycode[6], uint8_t modifier) {
    static char buf[64];
    size_t blen = sizeof(buf), size = 0;
    LOOPN(i, 6) {
        if (keycode[i] == HID_KEY_NONE) {
            buf[size] = '\0';
            break;
        }
        size += snprintf(buf + size, blen - size, "%s%s",
            i ? " | " : "", hid_keycode_str(keycode[i], modifier));
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
    if (sent && (modifier || klen) && ms != UINT32_MAX) {
        msleep(ms);
        sent = hid_report_keybd(to, 0, NULL, 0);
    }
    return sent;
}

void hid_handle_keybd(
    hid_target_t from, hid_keybd_report_t *rpt, hid_key_cb key_cb
) {
    int idx = -1;
    LOOPN(i, 8) { if (from & BIT(i)) idx = idx == -1 ? i : -2; }
    if (idx < 0 || !rpt) return;
    static uint8_t pmods[8], prevs[8][LEN(rpt->keycode)];
    uint8_t *next = rpt->keycode, *prev = prevs[idx];
    LOOPN(i, LEN(rpt->keycode)) {
        bool prev_found = false, next_found = false;
        LOOPN(j, LEN(rpt->keycode)) {
            if (prev[i] == next[j]) next_found = true;
            if (next[i] == prev[j]) prev_found = true;
        }
        if (prev[i] > HID_KEY_ERROR_UNDEFINED && !next_found) {
            if (key_cb) key_cb(prev[i], false);
            ESP_LOGI(TAG, "%s released", hid_keycode_str(prev[i], pmods[idx]));
        }
        if (next[i] > HID_KEY_ERROR_UNDEFINED && !prev_found) {
            if (key_cb) key_cb(next[i], true);
            ESP_LOGI(TAG, "%s pressed modifier %s",
                     hid_keycode_str(next[i], rpt->modifier),
                     hid_modifier_str(rpt->modifier));
        }
    }
    memcpy(prev, next, LEN(rpt->keycode));
    pmods[idx] = rpt->modifier;
}
