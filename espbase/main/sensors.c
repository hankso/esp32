/*
 * File: sensors.c
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2024/4/29 21:47:55
 */

#include "sensors.h"
#include "drivers.h"            // for smbus_xxx && i2c_xxx
#include "config.h"

static const char *TAG = "Sensor";

static UNUSED uint8_t maskread(uint8_t val, uint8_t mask) {
    const uint8_t lowest_bitmap[] = { 0, 1, 2, 0, 3, 5, 0, 8, 4, 7, 6 };
    return bitsread(val, lowest_bitmap[(mask & ~(mask - 1)) % 11], mask);
}

/******************************************************************************
 * Internal temperature sensor
 */

#ifdef SOC_TEMP_SENSOR_SUPPORTED
#   include "driver/temp_sensor.h"

static void temp_initialize() {
    temp_sensor_config_t temp_conf = TSENS_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( temp_sensor_set_config(temp_conf) );
    ESP_ERROR_CHECK( temp_sensor_start() );
    ESP_LOGI(TAG, "Temperature is %.3f celsius degree", temp_celsius());
}

float temp_celsius() {
    float deg;
    temp_sensor_read_celsius(&deg);
    return deg;
}
#else
float temp_celsius() { return 0; }
#endif

/******************************************************************************
 * Internal touch pad
 */

#ifdef CONFIG_BASE_USE_TPAD
#   include "soc/touch_sensor_periph.h"
#   include "driver/touch_pad.h"

static touch_pad_t tpad = TOUCH_PAD_MAX;

static touch_pad_t gpio2tpad(gpio_num_t pin) {
    LOOPN(i, SOC_TOUCH_SENSOR_NUM) {
        if (touch_sensor_channel_io_map[i] == pin) return i;
    }
    return TOUCH_PAD_MAX;
}

static void tpad_initialize() {
    if (( tpad = gpio2tpad(PIN_TPAD) ) == TOUCH_PAD_MAX) {
        ESP_LOGE(TAG, "Touch: invalid pin %d", PIN_TPAD);
        return;
    }
    ESP_ERROR_CHECK( touch_pad_init() );
    touch_pad_set_voltage(
        TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_1V);
    touch_pad_config(tpad, 0); // threshold=0
    touch_pad_filter_start(10); // period=10ms
    ESP_LOGI(TAG, "Touch pad is %d", tpad_read());
}

uint16_t tpad_read() {
    if (tpad == TOUCH_PAD_MAX) return 0;
    uint16_t val;
    touch_pad_read_filtered(tpad, &val);
    return val;
}
#else
uint16_t tpad_read() { return 0; }
#endif

/******************************************************************************
 * Touch Screen
 */

#ifdef CONFIG_BASE_USE_TSCN

#define TP_DEVICE_MODE  0x00
#define TP_GESTURE_ID   0x01
#define TP_NUM_TOUCHES  0x02
#define TP_P1_XH        0x03
#define TP_P1_XL        0x04
#define TP_P1_YH        0x05
#define TP_P1_YL        0x06
#define TP_P1_WEIGHT    0x07
#define TP_P1_MISC      0x08
#define TP_TH_GROUP     0x80
#define TP_TH_DIFF      0x85
#define TP_CTRL         0x86
#define TP_TIME_MONITOR 0x87
#define TP_TR_ACTIVE    0x88
#define TP_TR_MONITOR   0x89
#define TP_RADIAN_VALUE 0x91
#define TP_OFFSET_LR    0x92
#define TP_OFFSET_UD    0x93
#define TP_DIST_LR      0x94
#define TP_DIST_UD      0x95
#define TP_DIST_ZOOM    0x96
#define TP_LIB_VER_H    0xA1
#define TP_LIB_VER_L    0xA2
#define TP_CHIP_ID      0xA3
#define TP_INTR_MODE    0xA4
#define TP_POWER_MODE   0xA5
#define TP_FIRMW_MODE   0xA6
#define TP_PANEL_ID     0xA8
#define TP_RELEASE_ID   0xAF
#define TP_STATE        0xBC

static uint8_t tscn = 0;

static void tscn_status() {
    if (!tscn) return;
    const struct {
        const char *name;
        uint8_t reg;
    } lst[] = {
        { "TH_DIFF",            TP_TH_DIFF },
        { "CTRL",               TP_CTRL },
        { "RADIAN_VALUE",       TP_RADIAN_VALUE },
        { "TIME_MONITOR",       TP_TIME_MONITOR },
        { "PERIOD_ACTIVE",      TP_TR_ACTIVE },
        { "PERIOD_MONITOR",     TP_TR_MONITOR },
        { "OFFSET_LEFT_RIGHT",  TP_OFFSET_LR },
        { "OFFSET_UP_DOWN",     TP_OFFSET_UD },
        { "DIST_LEFT_RIGHT",    TP_DIST_LR },
        { "DIST_UP_DOWN",       TP_DIST_UD },
        { "DIST_ZOOM",          TP_DIST_ZOOM },
        { "INTR_MODE",          TP_INTR_MODE },
        { "POWER_MODE",         TP_POWER_MODE },
        { "FIRMWARE_MODE",      TP_FIRMW_MODE },
        { "PANEL_ID",           TP_PANEL_ID },
        { "RELEASE_ID",         TP_RELEASE_ID },
    };
#   ifdef CONFIG_BASE_AUTO_ALIGN
    size_t namelen = 0;
    LOOPN(i, LEN(lst)) { namelen = MAX(namelen, strlen(lst[i].name)); }
#   else
    size_t namelen = 16;
#   endif
    LOOPN(i, LEN(lst)) {
        uint8_t val;
        esp_err_t err = smbus_read_byte(NUM_I2C, tscn, lst[i].reg, &val);
        if (err) {
            ESP_LOGE(TAG, "Read touch screen failed: %s", esp_err_to_name(err));
            return;
        }
        printf("%*s: 0x%02X%c", namelen, regs[i].name, val, " \n"[i % 2]);
    }
    if (LEN(lst) % 2) putchar('\n');
}

static void tscn_initialize() {
    const char *chip;
    uint8_t vendor = 0x11, addr = 0x38;
    smbus_regval_t regs[] = {
        RT_RBYTE(TP_PANEL_ID), RT_RBYTE(TP_CHIP_ID),
        { TP_DEVICE_MODE, 0x00 }, { TP_TH_GROUP, 0x16 }, { TP_TR_ACTIVE, 0x0E }
    };
    esp_err_t err = smbus_probe(NUM_I2C, addr);
    if (!err) err = smbus_regtable(NUM_I2C, addr, regs, 2);
    if (!err) err = regs[0].val == vendor ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (!err) err = smbus_regtable(NUM_I2C, addr, regs + 2, 3);
    if (err) return;
    switch (regs[1].val) {
        case 0x06: chip = "FT6206"; break;
        case 0x36: chip = "FT6236"; break;
        case 0x64: chip = "FT6336"; break;
        default:   chip = "FT6X36"; break;
    }
    ESP_LOGI(TAG, "TSCN: found %s at I2C %d-%02X", chip, NUM_I2C, addr);
    tscn = addr;
#   ifdef CONFIG_BASE_DEBUG
    tscn_status();
#   endif
}

esp_err_t tscn_probe(tscn_data_t *dat) {
    if (!tscn) return ESP_ERR_INVALID_STATE;
    esp_err_t err = smbus_read_byte(NUM_I2C, tscn, TP_NUM_TOUCHES, &dat->num);
    if (!err) err = smbus_read_byte(NUM_I2C, tscn, TP_GESTURE_ID, &dat->ges);
    if (!err && dat->num > LEN(dat->pts)) err = ESP_ERR_NO_MEM;
    if (err || !dat->num) return err;
    const uint8_t gvals[] = { 0x00, 0x10, 0x14, 0x18, 0x1C, 0x48, 0x49 };
    uint8_t buf[6];
    LOOPN(i, dat->num) {
        if (( err = smbus_rregs(NUM_I2C, tscn, TP_P1_XH + 6 * i, buf, 6) ))
            break;
        dat->pts[i].evt  = buf[0] >> 6; // 0: press, 1: release, 2: contact
        dat->pts[i].id   = buf[2] >> 4;
        dat->pts[i].x    = (buf[0] & 0xF) << 8 | buf[1];
        dat->pts[i].y    = (buf[2] & 0xF) << 8 | buf[3];
        dat->pts[i].wt   = buf[4];      // touch pressure value
        dat->pts[i].area = buf[5] >> 4; // touch area value
    }
    LOOPN(i, LEN(gvals)) { if (dat->ges == gvals[i]) dat->ges = i; }
    return err;
}
#else // CONFIG_BASE_USE_TSCN
esp_err_t tscn_probe(tscn_data_t *d) {
    NOTUSED(d);
    return ESP_ERR_NOT_SUPPORTED;
}
#endif // CONFIG_BASE_USE_TSCN

/******************************************************************************
 * Multiple Screens
 */

#define MS_REG_CMD  0x80 // A0 = 0
#define MS_REG_DAT  0xC0 // A0 = 1

#define MS_SETAY0       0x00
#define MS_SETAY1       0x10
#define MS_PWROFF       0x2C
#define MS_PWRON        0x2F
#define MS_BIAS         0x30
#define     MS_BIAS_6   0x04
#define     MS_BIAS_7   0x03
#define     MS_BIAS_8   0x02
#define     MS_BIAS_9   0x00
#define     MS_BIAS_10  0x01
#define     MS_BIAS_11  0x05
#define MS_WAKEUP       0x38
#define MS_SLEEP        0x39
#define MS_OSCON        0x3A
#define MS_OSCOFF       0x3B
#define MS_DISPOFF      0x3C
#define MS_DISPON       0x3D
#define MS_SETSL0       0x40
#define MS_SETSL1       0x50
#define MS_DRV          0x60
#define     MS_DRV_SHL  0x08
#define     MS_DRV_ADC  0x04
#define     MS_DRV_EON  0x02
#define     MS_DRV_REV  0x01
#define MS_RESET        0x76
#define MS_SETDUTY0     0x90
#define MS_SETDUTY1     0xA0
#define MS_SETAX        0xC0
#define MS_RDRAM        0x77
#define MS_RDST         0x78
#define MS_VREF         0xB1
#define MS_FRCT         0xB2
#define MS_NOP          0xE3
#define MS_MTP          0x80 // enter MTP command mode
#define     MS_MTP_CTEN 0x1A
#define     MS_MTP_PEN  0xEC
#define     MS_MTP_ST   0x20
#define     MS_MTP_CTOF 0x26
#define     MS_MTP_MADR 0xA2
#define MS_IST          0x88 // repeat 4 times to enter IST command mode
#define     MS_IST_MAP  0x60 // change COM mapping (IST command mode)
#define MS_EXIT         0xE3 // return to normal mode
#define MS_DELAY        0xF0

typedef struct {
    uint8_t bus, addr;
    bool busy, adc, donb, resb;
} ms_ctx_t;

static esp_err_t ist3931_send(
    ms_ctx_t *ctx, uint8_t type, const uint8_t *buf, size_t len
) {
    if (!ctx->addr) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    size_t cnt = 0;
    uint8_t *ptr = (uint8_t *)buf, *tmp;
    while (cnt < len && (*ptr++ & MS_DELAY) != MS_DELAY) { cnt++; }
    if (cnt == 1) {
        smbus_write_byte(ctx->bus, ctx->addr, type, buf[0]);
    } else if (cnt > 1) {
        if (( err = EMALLOC(tmp, cnt * 2) )) return err;
        ptr = tmp;
        LOOPN(i, cnt) {
            *ptr++ = type;
            *ptr++ = buf[i];
        }
        err = smbus_wregs(ctx->bus, ctx->addr, tmp[0], tmp + 1, cnt * 2 - 1);
        TRYFREE(tmp);
        if (err) return err;
    }
    if (len > cnt) {
        msleep(buf[cnt] & 0xF);
        err = ist3931_send(ctx, type, buf + cnt + 1, len - cnt - 1);
    }
    return err;
}

static esp_err_t ist3931_set_window(ms_ctx_t *ctx, uint8_t x, uint8_t y) {
    uint8_t cmds[] = {
        MS_SETAY1 | bitnread(y, 4, 2),
        MS_SETAY0 | (y & 0xF),
        MS_SETAX  | (x & 0x1F)
    };
    return ist3931_send(ctx, MS_REG_CMD, cmds, sizeof(cmds));
}

static esp_err_t ist3931_read_status(ms_ctx_t *ctx) {
    if (!ctx->addr) return ESP_ERR_INVALID_STATE;
    uint8_t val;
    esp_err_t err = smbus_write_byte(ctx->bus, ctx->addr, MS_REG_CMD, MS_RDST);
    if (!err) {
        msleep(5);
        err = i2c_master_read_from_device(ctx->bus, ctx->addr, &val, 1, 20);
    }
    if (!err) {
        ctx->busy = bitread(val, 7);
        ctx->adc  = bitread(val, 6);
        ctx->donb = !bitread(val, 5);
        ctx->resb = !bitread(val, 4);
        printf("Busy: %d, ADC: %d, Display: %d, Reset: %d\n",
            ctx->busy, ctx->adc, ctx->donb, ctx->resb);
    }
    return err;
}

static esp_err_t ist3931_read_frame(ms_ctx_t *ctx) {
    if (!ctx->addr) return ESP_ERR_INVALID_STATE;
    uint8_t v[16 + 1], l = sizeof(v); memset(v, 0, l);
    esp_err_t err = ist3931_set_window(ctx, 0, 0);
    if (!err) err = smbus_write_byte(ctx->bus, ctx->addr, MS_REG_CMD, MS_RDRAM);
    if (!err) err = i2c_master_read_from_device(ctx->bus, ctx->addr, v, l, 50);
    if (!err) ESP_LOG_BUFFER_HEX(TAG, v + 1, l - 1);
    return err;
}

static ms_ctx_t ictx;

const uint8_t init_cmds[] = {
    MS_RESET,               // soft reset
    MS_DELAY | 0xF,         // delay 50ms
    MS_DELAY | 0xF,
    MS_DELAY | 0xF,
    MS_DELAY | 0x5,

    MS_IST, MS_IST,         // IST MAP = 0
    MS_IST, MS_IST,
    MS_IST_MAP,
    MS_DELAY | 0xA,         // delay 10ms
    MS_EXIT,

    MS_PWRON,               // VC = 1, VF = 1
    MS_BIAS,                // BIAS = 1/9
    MS_VREF, 0x3C,          // V0 = (VREF / 200 + 0.7) / BIAS = 9V

    MS_SETDUTY0 | 0,        // DUTY = 0x40
    MS_SETDUTY1 | 4,
    MS_DRV | MS_DRV_EON,    // EON = 1
    MS_FRCT, 0x71, 0x02,    // FPS = 3MHz / FRCT / DUTY = 75Hz
    MS_DELAY | 0xA,         // delay 10ms

    MS_DISPON,
    MS_DELAY | 0xA,         // delay 10ms
};

const uint8_t test_dats[] = {
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
};

void mscn_status() {
    ist3931_read_status(&ictx);
    ist3931_read_frame(&ictx);
}

static void mscn_initialize() {
    ictx.bus = NUM_I2C;
    ictx.addr = 0x3F;
    esp_err_t err = smbus_probe(ictx.bus, ictx.addr);
    if (!err) err = ist3931_read_status(&ictx);
    if (!err) err = ist3931_send(&ictx, MS_REG_CMD, init_cmds, LEN(init_cmds));
    if (!err) err = ist3931_set_window(&ictx, 0, 4);
    if (!err) err = ist3931_send(&ictx, MS_REG_DAT, test_dats, LEN(test_dats));
    if (err) {
        ictx.addr = 0;
        return;
    }
    ESP_LOGI(TAG, "MSCN: found IST3931 at I2C %d-%02X", ictx.bus, ictx.addr);
}

/******************************************************************************
 * Distance Measurement
 */

#ifdef CONFIG_BASE_VLX_SENSOR

typedef struct {
    uint8_t bus, addr, tcc, msrc, dss, pre, final, stop, spad;
    uint16_t pre_pclks, final_pclks, pre_mclks, final_mclks, msrc_mclks;
    uint32_t pre_us, final_us, msrc_us, budget_us;
} vlx_ctx_t;

#   define MACRO_PERIOD(pclks) ((2304U * 1655U * (pclks) + 500) / 1000)
// VL53L0X_CalcTimeoutUs
static uint32_t vlx_mclks2us(uint16_t mclks, uint8_t vcsel_period_pclks) {
    uint32_t macro_period_ns = MACRO_PERIOD(vcsel_period_pclks);
    return (mclks * macro_period_ns + macro_period_ns / 2) / 1000;
}

// VL53L0X_CalcTimeoutMclks
static uint16_t vlx_us2mclks(uint32_t us, uint8_t vcsel_period_pclks) {
    uint32_t macro_period_ns = MACRO_PERIOD(vcsel_period_pclks);
    return (us * 1000 - macro_period_ns / 2) / macro_period_ns;
}
#   undef MACRO_PERIOD

// VL53L0X_DecodeTimeout (LSB * 2^MSB) + 1
static uint16_t vlx_val2tout(uint16_t val) {
    return ((val & 0xFF) << (val >> 8)) + 1;
}

// VL53L0X_EncodeTimeout
static uint16_t vlx_tout2val(uint16_t tout) {
    uint16_t msb = 0, lsb = tout ? tout - 1 : 0;
    while (lsb > 0xFF) { lsb >>= 1; msb++; }
    return (msb << 8) | lsb;
}

// VL53L0X_GetInfoFromDevice + VL53L0X_SetSpadMap
static esp_err_t vlx_sync_spad(vlx_ctx_t *ctx) {
    smbus_regval_t regs[] = {
        // VL53L0X_GetSpadInfo
        { 0x80, 0x01 }, { 0xFF, 0x01 }, { 0x00, 0x00 }, { 0xFF, 0x06 },
        RT_SBITS(0x83, 0x04),           { 0xFF, 0x07 }, { 0x81, 0x01 },
        { 0x80, 0x01 }, { 0x94, 0x6B }, { 0x83, 0x00 },
        RT_WAIT1(0x83, 0xFF, 100),      { 0x83, 0x01 }, RT_RBYTE(0x92),
        { 0x81, 0x00 }, { 0xFF, 0x06 }, RT_CBITS(0x83, 0x04),
        { 0xFF, 0x01 }, { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 },
        // VL53L0X_GetSpadMap
        RT_RBYTE(0xB0), RT_RBYTE(0xB1), RT_RBYTE(0xB2),
        RT_RBYTE(0xB3), RT_RBYTE(0xB4), RT_RBYTE(0xB5),
        // VL53L0X_SetSpadRef
        { 0xFF, 0x01 }, { 0x4F, 0x00 }, { 0x4E, 0x2C },
        { 0xFF, 0x00 }, { 0xB6, 0xB4 },
    }, *spad = RT_FIND_REG(regs, 0x92), *smap = RT_FIND_REG(regs, 0xB0);
    esp_err_t err = smbus_regtable(ctx->bus, ctx->addr, regs, LEN(regs));
    if (err || !spad || !smap) return err ?: ESP_FAIL;
    uint8_t first = (spad->val & 0x80) ? 12 : 0, total = spad->val & 0x7F;
    for (int cnt = 0, i = 0; i < 48; i++) {
        smap[i / 8].reg &= 0xFF;
        if (i < first || cnt == total) {
            smap[i / 8].val &= ~BIT(i % 8);
        } else if (bitread(smap[i / 8].val, i % 8)) {
            cnt++;
        }
    }
    return smbus_regtable(ctx->bus, ctx->addr, smap, 6);
}

// VL53L0X_GetMeasurementTimingBudget
static esp_err_t vlx_sync_timing(vlx_ctx_t *ctx, bool get) {
    smbus_regval_t regs[] = {
        RT_RBYTE(0x01), RT_RBYTE(0x46),
        RT_RBYTE(0x50), RT_RWORD(0x51),
        RT_RBYTE(0x70), RT_RWORD(0x71),
    };
    esp_err_t err = smbus_regtable(ctx->bus, ctx->addr, regs, LEN(regs));
    if (!err) {
        ctx->msrc =  bitread(regs[0].val, 2);
        ctx->dss =   bitread(regs[0].val, 3);
        ctx->tcc =   bitread(regs[0].val, 4);
        ctx->pre =   bitread(regs[0].val, 6);
        ctx->final = bitread(regs[0].val, 7);

        ctx->pre_pclks =    (regs[2].val + 1) << 1;
        ctx->final_pclks =  (regs[4].val + 1) << 1;
        ctx->pre_mclks =    vlx_val2tout(regs[3].val);
        ctx->msrc_mclks =   regs[1].val + 1;
        ctx->final_mclks =  vlx_val2tout(regs[5].val);
        if (ctx->pre) ctx->final_mclks -= ctx->pre_mclks;

        ctx->pre_us =   vlx_mclks2us(ctx->pre_mclks,      ctx->pre_pclks);
        ctx->msrc_us =  vlx_mclks2us(ctx->msrc_mclks + 1, ctx->pre_pclks);
        ctx->final_us = vlx_mclks2us(ctx->final_mclks,    ctx->final_pclks);
    }
    uint32_t us = (get ? 1910 : 1320) + 960;        // Start + End Overhead
    if (ctx->tcc) us += ctx->msrc_us + 590;         // TccOverhead
    if (ctx->dss) {
        us += 2 * (ctx->msrc_us + 690);             // DssOverhead
    } else if (ctx->msrc) {
        us += ctx->msrc_us + 660;                   // MsrcOverhead
    }
    if (ctx->pre) us += ctx->pre_us + 660;          // PreRangeOverhead
    if (ctx->final) us += ctx->final_us + 550;      // FinalRangeOverhead
    if (!err && get) ctx->budget_us = us;
    if (err || get || !ctx->final) return err;
    us -= ctx->final_us;
    if (ctx->budget_us < MIN(us, 20000))
        return ESP_ERR_INVALID_ARG;                 // Too low/high budget
    uint32_t final_us = ctx->budget_us - us;
    uint16_t final_mclks = vlx_us2mclks(final_us, ctx->final_pclks);
    if (ctx->pre) final_mclks += ctx->pre_mclks;
    uint16_t final_val = vlx_tout2val(final_mclks);
    return smbus_write_word(ctx->bus, ctx->addr, 0x71, final_val);
}

// VL53L0X_StartMeasurement
static esp_err_t vlx_startc(vlx_ctx_t *ctx, uint32_t period_ms) {
    smbus_regval_t regs[] = {
        { 0x80, 0x01 }, { 0xFF, 0x01 }, { 0x00, 0x00 }, { 0x91, 0x3C }, // stop
        { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 }, RT_RWORD(0xF8),
    };
    esp_err_t err = smbus_regtable(ctx->bus, ctx->addr, regs, LEN(regs));
    if (err) return err;
    if (period_ms == UINT32_MAX)
        return smbus_write_byte(ctx->bus, ctx->addr, 0x00, 0x01); // singleshot
    if (!period_ms)
        return smbus_write_byte(ctx->bus, ctx->addr, 0x00, 0x02); // backtoback
    uint32_t v = period_ms * (regs[7].val ?: 1);
    uint8_t buf[] = { v >> 24, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF };
    err = smbus_wregs(ctx->bus, ctx->addr, 0x04, buf, LEN(buf));
    return err ?: smbus_write_byte(ctx->bus, ctx->addr, 0x00, 0x04); // timed
}

// VL53L0X_StopMeasurement
static UNUSED esp_err_t vlx_stopc(vlx_ctx_t *ctx) {
    smbus_regval_t regs[] = {
        { 0x00, 0x01 }, { 0xFF, 0x01 }, { 0x00, 0x00 },
        { 0x91, 0x00 }, { 0x00, 0x01 }, { 0xFF, 0x00 },
    };
    return smbus_regtable(ctx->bus, ctx->addr, regs, LEN(regs));
}

// VL53L0X_PerformSingleRangingMeasurement
static uint16_t vlx_range(vlx_ctx_t *ctx, bool oneshot) {
    smbus_regval_t regs[] = {
        RT_WAIT0(0x00, 0x01, 100),
        RT_WAIT1(0x13, 0x07, 100), RT_RWORD(0x1E), { 0x0B, 0x01 }
    }, *p = RT_FIND_REG(regs, 0x1E);
    return (
        oneshot ? (
            vlx_startc(ctx, UINT32_MAX) ||
            smbus_regtable(ctx->bus, ctx->addr, regs, LEN(regs))
        ) : smbus_regtable(ctx->bus, ctx->addr, regs + 1, LEN(regs) - 1)
    ) ? UINT16_MAX : p->val;
}

static esp_err_t vlx_init(vlx_ctx_t *ctx) {
    smbus_regval_t pre[] = {
        // VL53L0X_DataInit
        { 0x88, 0x00 }, { 0x80, 0x01 }, { 0xFF, 0x01 }, { 0x00, 0x00 },
        RT_RBYTE(0x91), { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 },
        //   - set signal rate limit
        RT_SBITS(0x60, 0x12), RT_WWORD(0x44, 0x20),     { 0x01, 0xFF },
    }, post[] = {
        // VL53L0X_LoadTuningSettings
        { 0xFF, 0x01 }, { 0x00, 0x00 }, { 0xFF, 0x00 }, { 0x09, 0x00 },
        { 0x10, 0x00 }, { 0x11, 0x00 }, { 0x24, 0x01 }, { 0x25, 0xFF },
        { 0x75, 0x00 }, { 0xFF, 0x01 }, { 0x4E, 0x2C }, { 0x48, 0x00 },
        { 0x30, 0x20 }, { 0xFF, 0x00 }, { 0x30, 0x09 }, { 0x54, 0x00 },
        { 0x31, 0x04 }, { 0x32, 0x03 }, { 0x40, 0x83 }, { 0x46, 0x25 },
        { 0x60, 0x00 }, { 0x27, 0x00 }, { 0x50, 0x06 }, { 0x51, 0x00 },
        { 0x52, 0x96 }, { 0x56, 0x08 }, { 0x57, 0x30 }, { 0x61, 0x00 },
        { 0x62, 0x00 }, { 0x64, 0x00 }, { 0x65, 0x00 }, { 0x66, 0xA0 },
        { 0xFF, 0x01 }, { 0x22, 0x32 }, { 0x47, 0x14 }, { 0x49, 0xFF },
        { 0x4A, 0x00 }, { 0xFF, 0x00 }, { 0x7A, 0x0A }, { 0x7B, 0x00 },
        { 0x78, 0x21 }, { 0xFF, 0x01 }, { 0x23, 0x34 }, { 0x42, 0x00 },
        { 0x44, 0xFF }, { 0x45, 0x26 }, { 0x46, 0x05 }, { 0x40, 0x40 },
        { 0x0E, 0x06 }, { 0x20, 0x1A }, { 0x43, 0x40 }, { 0xFF, 0x00 },
        { 0x34, 0x03 }, { 0x35, 0x44 }, { 0xFF, 0x01 }, { 0x31, 0x04 },
        { 0x4B, 0x09 }, { 0x4C, 0x05 }, { 0x4D, 0x04 }, { 0xFF, 0x00 },
        { 0x44, 0x00 }, { 0x45, 0x20 }, { 0x47, 0x08 }, { 0x48, 0x28 },
        { 0x67, 0x00 }, { 0x70, 0x04 }, { 0x71, 0x01 }, { 0x72, 0xFE },
        { 0x76, 0x00 }, { 0x77, 0x00 }, { 0xFF, 0x01 }, { 0x0D, 0x01 },
        { 0xFF, 0x00 }, { 0x80, 0x01 }, { 0x01, 0xF8 }, { 0xFF, 0x01 },
        { 0x8E, 0x01 }, { 0x00, 0x01 }, { 0xFF, 0x00 }, { 0x80, 0x00 },
        // VL53L0X_SetGpioConfig
        { 0x0A, 0x04 }, RT_CBITS(0x84, 0x10), { 0x0B, 0x01 },
    }, calib[] = {
        // VL53L0X_PerformRefCalibration
        //   - perform vhv calibration
        { 0x01, 0x01 }, { 0x00, 0x41 }, RT_WAIT1(0x13, 0x07, 100),
        { 0x0B, 0x01 }, { 0x00, 0x00 },
        //   - perform phase calibration
        { 0x01, 0x02 }, { 0x00, 0x01 }, RT_WAIT1(0x13, 0x07, 100),
        { 0x0B, 0x01 }, { 0x00, 0x00 },
        { 0x01, 0xE8 }, // calibration end
    };
    esp_err_t err = smbus_regtable(ctx->bus, ctx->addr, pre, LEN(pre));
    if (!err) err = vlx_sync_spad(ctx);
    if (!err) err = smbus_regtable(ctx->bus, ctx->addr, post, LEN(post));
    if (!err) err = vlx_sync_timing(ctx, true);
    if (!err) err = smbus_write_byte(ctx->bus, ctx->addr, 0x01, 0xE8);
    if (!err) err = vlx_sync_timing(ctx, false);
    if (!err) err = smbus_regtable(ctx->bus, ctx->addr, calib, LEN(calib));
    if (!err) ctx->stop = RT_FIND_REG(pre, 0x91)->val;
    return err;
}

static vlx_ctx_t vctx;

static void vlx_initialize() {
    vctx.bus = I2C_NUM_0;
    vctx.addr = 0x29;
    esp_err_t err = smbus_probe(vctx.bus, vctx.addr);
    if (!err) err = vlx_init(&vctx);
    if (!err) err = vctx.stop == 0x3C ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err) { vctx.addr = 0; return; }
    ESP_LOGI(TAG, "VLX: found VL53L0X at I2C %d-%02X", vctx.bus, vctx.addr);
}

uint16_t vlx_probe() { return vlx_range(&vctx, true); }
#else
uint16_t vlx_probe() { return UINT16_MAX; }
#endif

/******************************************************************************
 * Ambient Light and Temperature Sensor
 */

esp_err_t gy39_measure(gy39_data_t *d) {
    uint8_t addr = 0x5B, b[0x0E];
    esp_err_t err = smbus_rregs(NUM_I2C, addr, 0x00, b, sizeof(b));
    if (err) return err;
    d->brightness = 1e-2 * ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
    d->temperature = 1e-2 * ((b[4] << 8) | b[5]);
    d->atmosphere = 1e-5 * ((b[6] << 24) | (b[7] << 16) | (b[8] << 8) | b[9]);
    d->humidity = 1e-2 * ((b[10] << 8) | b[11]);
    d->altitude = (b[12] << 8) | b[13];
    return err;
}

// I2C Ambient Light Sensor
// 7bit I2C address of the OPT3001 is configurable by ADDR PIN.
// Basic address is 0b010001XX where `XX` are:
//      ADDR -> GND: 0b00 (0x44)
//      ADDR -> VDD: 0b01 (0x45)
//      ADDR -> SDA: 0b10 (0x46)
//      ADDR -> SCL: 0b11 (0x47)

#ifdef CONFIG_BASE_ALS_TRACK
static uint8_t als_addr[ALS_NUM] = {
    0b01000100, 0b01000101,         // east, west
    0b01000110, 0b01000111          // south, north
};

static void als_initialize() {
    uint8_t bus = NUM_I2C, addr;
    smbus_regval_t regs[] = {
        RT_RWORD(0x7E), RT_RWORD(0x7F),
        // 15:12 RN[3:0] = 1100b    automatic full-scale
        // 11    CT      = 1b       conversion time 800ms
        // 10:9  M[1:0]  = 11b      continuous mode
        // 8:5                      read-only
        // 4     L       = 1b       latched window-style
        // 3     POL     = 0b       INT pin reports active low
        // 2     ME      = 0b       mask exponent off
        // 1:0   FC[1:0] = 10b      four fault counts
        RT_WWORD(0x01, 0xCE12),
        RT_WWORD(0x02, 0x0800),     // low-limit 20.48 lux
        RT_WWORD(0x03, 0xBFFF),     // high-limit 83865 lux
    };
    LOOPN(i, ALS_NUM) {
        addr = als_addr[i];
        als_addr[i] = 0;
        if (smbus_probe(bus, addr) || smbus_regtable(bus, addr, regs, LEN(regs)))
            continue;
        ESP_LOGI(TAG, "ALS: found %c%c %04X at I2C %d-%02X",
                 regs[0].val >> 8, regs[0].val & 0xFF, regs[1].val, bus, addr);
        als_addr[i] = addr;
    }
}

float als_brightness(uint8_t idx) {
    uint16_t val;
    if (idx > ALS_NUM || !als_addr[idx]) return 0;
    if (smbus_read_word(NUM_I2C, als_addr[idx], 0x00, &val)) return 0;
    // Equation 3 at Page 20 of OPT3001 datasheet:
    //   lux = 0.01 * 2^E[3:0] * R[11:0]
    return 0.01 * (1 << (val >> 12)) * (val & 0xFFF);
}

static esp_err_t als_atdeg(int hdeg, int vdeg, float vals[ALS_NUM + 1]) {
    esp_err_t err = pwm_set_degree(hdeg, vdeg);
    if (err) return err;
    if ((hdeg + vdeg) > -2) msleep(100);
    memset(vals, 0, sizeof(float) * (ALS_NUM + 1));
    LOOPN(i, ALS_NUM) {
        LOOPN(j, 3) {
            vals[i] += als_brightness(i);
            if (j && !vals[i]) break;
            msleep(20);
        }
        vals[ALS_NUM] += (vals[i] /= 3);
    }
    if (!vals[ALS_NUM]) err = ESP_ERR_INVALID_STATE;
    return err;
}

esp_err_t als_tracking(als_track_t method, int *hdeg, int *vdeg) {
#define ALS_LOG(h, v, b) ESP_LOGI(TAG, "ALS: H %3d V %3d %8.2f lux", h, v, b)
    float bmax = 0, bmin = 1e10, btmp, bval[ALS_NUM + 1];
    esp_err_t err = (hdeg && vdeg) ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (!err) err = pwm_get_degree(hdeg, vdeg);
    if (!err) err = als_atdeg(-1, -1, bval);
    if (err) return err;
    if (method <= ALS_TRACK_3) {                    // maximize brightness
        for (int i = 0, v = 0; v < 90; i++, v += 6) {
            for (int h = 0; h < 180; h += 5) {
                int htmp = i % 2 ? (180 - h) : h;   // S line scanning
                if (( err = als_atdeg(htmp, v, bval) )) return err;
                if (bval[method] > bmax) {
                    ALS_LOG(htmp, v, bval[method]);
                    bmax = bval[method];
                    *hdeg = htmp;
                    *vdeg = v;
                }
            }
        }
    } else if (method == ALS_TRACK_H) {             // minimize diff of E&W
        for (int h = 0; h < 180; h += 15) {
            if (( err = als_atdeg(h, -1, bval) )) return err;
            bmax = MAX(bmax, MAX(bval[0], bval[1]));
            if (( btmp = ABSDIFF(bval[0], bval[1]) ) < bmin) {
                ALS_LOG(h, *vdeg, bval[ALS_NUM]);
                bmin = btmp;
                *hdeg = h;
            }
            msleep(200);
        }
    } else if (method == ALS_TRACK_V) {             // minimize diff of N&S
        for (int v = 0; v < 90; v += 9) {
            if (( err = als_atdeg(-1, v, bval) )) return err;
            bmax = MAX(bmax, MAX(bval[2], bval[3]));
            if (( btmp = ABSDIFF(bval[2], bval[3]) ) < bmin) {
                ALS_LOG(*hdeg, v, bval[ALS_NUM]);
                bmin = btmp;
                *vdeg = v;
            }
            msleep(200);
        }
    } else if (method == ALS_TRACK_A) {             // TODO: test and debug
        float bnew[ALS_NUM + 1], dh, dv, lr = 0.01, tol = 2;
        int h = *hdeg < 90 ? 10 : -10, v = *vdeg < 45 ? 10 : -10;
        LOOPN(i, 10) {
            if (( err = als_atdeg(*hdeg + h, *vdeg + v, bnew) )) return err;
            btmp = lr * (bnew[ALS_NUM] - bval[ALS_NUM]);
            *hdeg += (h = (dh = btmp / h));
            *vdeg += (v = (dv = btmp / v));
            ESP_LOGI(TAG, "val=%.6f, new=%.6f, dh=%.6f, dv=%.6f, h=%d, v=%d",
                     bval[ALS_NUM], bnew[ALS_NUM], dh, dv, h, v);
            bval[ALS_NUM] = bnew[ALS_NUM];
            if ((ABS(dh) + ABS(dv)) < tol) break;
            ALS_LOG(*hdeg, *vdeg, bnew[ALS_NUM]);
            msleep(200);
        }
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    if (bmax != 0 || bmin != 1e10) return pwm_set_degree(*hdeg, *vdeg);
    return ESP_ERR_NOT_FOUND;
}
#else
float als_brightness(int i) { return 0; NOTUSED(i); }
esp_err_t als_tracking(als_track_t i, int *h, int *v) {
    return ESP_ERR_NOT_SUPPORTED; NOTUSED(i); NOTUSED(h); NOTUSED(v);
}
#endif // CONFIG_BASE_ALS_TRACK

/******************************************************************************
 * BQ25895
 */

#define PWR_ILIMIT 0x00 // input current limit
#define PWR_CONADC 0x02 // ADC control
#define PWR_ICHARG 0x04 // charging current limit
#define PWR_WATDOG 0x07 // watch dog timer
#define PWR_BATFET 0x09 // battery FET
#define PWR_STATUS 0x0B // chip status
#define PWR_ADCBAT 0x0E // measured battery voltage
#define PWR_ADCSYS 0x0F // measured output voltage
#define PWR_ADCBUS 0x11 // measured type-c input voltage
#define PWR_ADCCHG 0x12 // measured charging current
#define PWR_SYSTEM 0x14 // device version & reset

static uint8_t pwr = 0;

static void pwr_initialize() {
    uint8_t val, ver = 0b111, addr = 0x6A;
    esp_err_t err = smbus_probe(NUM_I2C, addr);
    if (!err) err = smbus_read_byte(NUM_I2C, addr, PWR_SYSTEM, &val);
    if (!err) err = bitsread(val, 3, ver) == ver ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (!err) err = smbus_write_byte(NUM_I2C, addr, PWR_SYSTEM, val | 0x80);
    if (!err) err = smbus_write_byte(NUM_I2C, addr, PWR_WATDOG, 0b10001101);
    if (!err) err = smbus_write_byte(NUM_I2C, addr, PWR_BATFET, 0b01001000);
    if (err) return;
    ESP_LOGI(TAG, "PWR: found BQ25895 at I2C %d-%02X", NUM_I2C, addr);
    pwr = addr;
#ifdef CONFIG_BASE_DEBUG
    pwr_status();
#endif
}

static int cumsum(uint8_t byte, int base, int step) {
    LOOPN(i, 7) {
        base += bitread(byte, i) * BIT(i) * step;
    }
    return base;
}

static UNUSED uint8_t cumsub(int val, int step) {
    uint8_t byte = 0;
    LOOPND(i, 7) {
        int tmp = BIT(i) * step;
        if (val >= tmp) {
            val -= tmp;
            byte |= BIT(i);
        }
    }
    return byte;
}

void pwr_status() {
    if (!pwr) return;
    uint8_t reg[0x15];
    esp_err_t err = smbus_read_byte(NUM_I2C, pwr, PWR_CONADC, reg + 0);
    if (!err) err = smbus_read_byte(NUM_I2C, pwr, PWR_ADCBUS, reg + 1);
    if (!err) {
        bool continuous = bitread(reg[0], 6);
        bool vbuspluged = bitread(reg[1], 7);
        if (!continuous) {
            err = smbus_write_byte(NUM_I2C, pwr, PWR_CONADC, reg[0] & 0x80);
            if (!err) msleep(1000); // wait 1s for ADC
        }
        if (!err && continuous && !vbuspluged) {
            // stop continuous ADC to save power when no VBUS plugged in
            err = smbus_write_byte(NUM_I2C, pwr, PWR_CONADC, reg[0] & ~0x40);
        }
        if (!err && !continuous && vbuspluged) {
            // start continuous ADC to save time when VBUS plugged in
            err = smbus_write_byte(NUM_I2C, pwr, PWR_CONADC, reg[0] | 0x40);
        }
    }
    if (!err) err = smbus_rregs(NUM_I2C, pwr, 0x00, reg, sizeof(reg));
    if (err) return;
    const char * INPTYPE[] = {
        "No input", "USB Host SDP", "USB CDP(1.5A)", "USB DCP(3.25A)",
        "ADJ DCP(1.5A)", "Unknown(0.5A)", "Non-standard(<2.4A)", "OTG"
    };
    const char * CHGSTAT[] = {
        "Discharging", "Pre-charge", "Fast-charge", "Charge done"
    };
    const char * CHGERR[] = {
        "Normal", "Input", "TS OVR", "TM EXP"
    };
    printf( // ~3KB
        "REG00 | Iin LIM : %4dmA | ILIM PIN:   %4d | HIZ MODE: %6d\n"
        "REG01 | Vin LIM : %4dmV | BST COLD: %5d%% | BST HOT : %6s\n"
        "REG02 | BST FREQ: %4sHz | ADC CONV:   %4d | ADC RATE: %6s\n"
        "REG02 | ICO EN  :   %4d | HVDCP EN:   %4d | MAXC EN : %6d\n"
        "REG02 | D+- XRCE:   %4d | D+- AUTO: %6d\n"
        "REG03 | Vsys MIN: %4dmV | BAT LOAD:   %4d | WDT Rst : %6d\n"
        "REG03 | OTG EN  :   %4d | CHG EN  : %6d\n"
        "REG04 | Ichg LIM: %4dmA | PUMPX EN: %6d\n"
        "REG05 | Ipre LIM: %4dmA | Ipos LIM: %4dmA\n"
        "REG06 | Vchg LIM: %4dmV | Vbat LOW: %4dmV | Vrechg  : %4dmV\n"
        "REG07 | TERM EN :   %4d | STAT DIS:   %4d | TIMER EN:   %4d\n"
        "REG07 | WDT TOUT:  %4ds | CHG TOUT:  %2dhrs\n"
        "REG08 | BAT COMP: %4dmR | Vclamp  : %4dmV | Treg    : %4d°C\n"
        "REG09 | ICO XOCE:   %4d | TMR2X EN:   %4d | PUMPX VC:  U%d-D%d\n"
        "REG09 | BFET DIS:   %4d | BFET DLY:   %4d | BFET RST: %6d\n"
        "REG0A | Vboost  : %4dmV\n"
        "REG0B | PG STAT :   %4d | Vbat<sys:   %4d | SDP STAT: USB%d00\n"
        "REG0B | Vbus ST : %s\n"
        "REG0B | CHG STAT: %s\n"
        "REG0C | WDT EXP :   %4d | BST ERR :   %4d | BAT OVP : %6d\n"
        "REG0C | CHG ERR : %6s | NTC ERR :%s %s\n"
        "REG0D | Vin DPM : %4dmV (%s)\n"
        "REG0E | Treg ST :   %4d | Vbat    : %4dmV\n"
        "REG0F | Vsys    : %4dmV\n"
        "REG10 | TSPCT   : %5.2f%%\n"
        "REG11 | Vbus GD :   %4d | Vbus    : %4dmV\n"
        "REG12 | Ichg    : %4dmA\n"
        "REG13 | Vdpm ST :   %4d | Idpm ST :   %4d | Idpm LIM: %4dmA\n"
        "REG14 | REG RST :   %4d | ICO OPT :   %4d\n",
        cumsum(reg[0] & 0x3F, 100, 50), bitread(reg[0], 7), bitread(reg[0], 6),
        cumsum(reg[1] & 0x1F, 0, 100), bitread(reg[1], 5) ? 80 : 77,
        bitsread(reg[1], 6, 0b11) == 0b11 ? "N/A" : "VTH",
        bitread(reg[2], 5) ? "500K" : "1.5M", bitread(reg[2], 7),
        bitread(reg[2], 6) ? "1Hz" : "Once",
        bitread(reg[2], 4), bitread(reg[2], 3), bitread(reg[2], 2),
        bitread(reg[2], 1), bitread(reg[2], 0),
        cumsum(bitnread(reg[3], 1, 3), 3000, 100), bitread(reg[3], 7),
        bitread(reg[3], 6), bitread(reg[3], 5), bitread(reg[3], 4),
        cumsum(reg[4] & 0x7F, 0, 64), bitread(reg[4], 7),
        cumsum(reg[5] >> 4, 64, 64), cumsum(reg[5] & 0xF, 64, 64),
        cumsum(reg[6] >> 2, 3840, 16), bitread(reg[6], 1) ? 3000 : 2800,
        cumsum(reg[6] >> 2, 3840, 16) - (bitread(reg[6], 0) ? 200 : 100),
        bitread(reg[7], 7), bitread(reg[7], 6), bitread(reg[7], 3),
        "\x00\x28\x50\xA0"[bitnread(reg[7], 4, 2)],
        "\x05\x08\x0C\x14"[bitnread(reg[7], 1, 2)],
        cumsum(bitnread(reg[8], 5, 3), 0, 20),
        cumsum(bitnread(reg[8], 2, 3), 0, 20),
        60 + 20 * (reg[8] & 0b11),
        bitread(reg[9], 7), bitread(reg[9], 6),
        bitread(reg[9], 1), bitread(reg[9], 0),
        bitread(reg[9], 5), bitread(reg[9], 3), bitread(reg[9], 2),
        cumsum(reg[10] >> 4, 4550, 64),
        bitread(reg[11], 2), bitread(reg[11], 0), bitread(reg[11], 1) ? 5 : 1,
        INPTYPE[reg[11] >> 5], CHGSTAT[bitnread(reg[11], 3, 2)],
        bitread(reg[12], 7), bitread(reg[12], 6), bitread(reg[12], 3),
        CHGERR[bitnread(reg[12], 4, 2)],
        bitread(reg[12], 2) ? "  Boost" : reg[12] & 0b11 ? "   Buck" : "",
        bitread(reg[12], 1) ? "Hot" : bitread(reg[12], 0) ? "Cold" : "Normal",
        cumsum(reg[13] & 0x7F, 2600, 100),
        bitread(reg[13], 7) ? "absolute" : "relative",
        bitread(reg[14], 7), cumsum(reg[14] & 0x7F, 2304, 20),
        cumsum(reg[15] & 0x7F, 2304, 20),
        cumsum(reg[16] & 0x7F, 21000, 465) / 1e3,
        bitread(reg[17], 7),
        bitread(reg[17], 7) ? cumsum(reg[17] & 0x7F, 2600, 100) : 0,
        cumsum(reg[18] & 0x7F, 0, 50),
        bitread(reg[19], 7), bitread(reg[19], 6),
        cumsum(reg[19] & 0x3F, 100, 50),
        bitread(reg[20], 7), bitread(reg[20], 6)
    );
}

/******************************************************************************
 * Sensor API
 */

void sensors_initialize() {
#ifdef SOC_TEMP_SENSOR_SUPPORTED
    temp_initialize();
#endif
#ifdef CONFIG_BASE_USE_TPAD
    tpad_initialize();
#endif
#ifdef CONFIG_BASE_USE_TSCN
    tscn_initialize();
#endif
#ifdef CONFIG_BASE_VLX_SENSOR
    vlx_initialize();
#endif
#ifdef CONFIG_BASE_ALS_TRACK
    als_initialize();
#endif
    pwr_initialize();
    mscn_initialize();
}
