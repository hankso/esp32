/*
 * File: sensors.c
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2024/4/29 21:47:55
 */

#include "sensors.h"
#include "drivers.h"

static const char *TAG = "Sensor";

#define bitsread(v, o, b)   ( ((v) >> (o)) & (b) )
#define bitnread(v, o, n)   bitsread((v), (o), (1 << (n)) - 1)
#define bitread(v, o)       bitsread((v), (o), 1)

static UNUSED uint8_t maskread(uint8_t val, uint8_t mask) {
    static const uint8_t lowest_bitmap[] = { 0, 1, 2, 0, 3, 5, 0, 8, 4, 7, 6 };
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
        const char *regname;
        uint8_t regaddr;
    } regs[] = {
        { "TH_DIFF", TP_TH_DIFF },
        { "CTRL", TP_CTRL },
        { "RADIAN_VALUE", TP_RADIAN_VALUE },
        { "TIME_MONITOR", TP_TIME_MONITOR },
        { "PERIOD_ACTIVE", TP_TR_ACTIVE },
        { "PERIOD_MONITOR", TP_TR_MONITOR },
        { "OFFSET_LEFT_RIGHT", TP_OFFSET_LR },
        { "OFFSET_UP_DOWN", TP_OFFSET_UD },
        { "DIST_LEFT_RIGHT", TP_DIST_LR },
        { "DIST_UP_DOWN", TP_DIST_UD },
        { "DIST_ZOOM", TP_DIST_ZOOM },
        { "INTR_MODE", TP_INTR_MODE },
        { "POWER_MODE", TP_POWER_MODE },
        { "FIRMWARE_MODE", TP_FIRMW_MODE },
        { "PANEL_ID", TP_PANEL_ID },
        { "RELEASE_ID", TP_RELEASE_ID },
    };
#   ifdef CONFIG_BASE_AUTO_ALIGN
    size_t namelen = 0;
    LOOPN(i, LEN(regs)) { namelen = MAX(namelen, strlen(regs[i].regname)); }
#   else
    size_t namelen = 16;
#   endif
    LOOPN(i, LEN(regs)) {
        uint8_t val;
        esp_err_t err = smbus_read_byte(NUM_I2C, tscn, regs[i].regaddr, &val);
        if (err) break;
        printf("%*s: 0x%02X%c", namelen, regs[i].regname, val, " \n"[i % 2]);
    }
    if (LEN(regs) % 2) putchar('\n');
}

static void tscn_initialize() {
    const char *chip;
    uint8_t val, vendor = 0x11, addr = 0x38;
    esp_err_t err = smbus_probe(NUM_I2C, addr);
    if (!err) err = smbus_read_byte(NUM_I2C, addr, TP_PANEL_ID, &val);
    if (!err) err = val == vendor ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (!err) err = smbus_read_byte(NUM_I2C, addr, TP_CHIP_ID, &val);
    if (!err) err = smbus_write_byte(NUM_I2C, addr, TP_DEVICE_MODE, 0x00);
    if (!err) err = smbus_write_byte(NUM_I2C, addr, TP_TH_GROUP, 0x16);
    if (!err) err = smbus_write_byte(NUM_I2C, addr, TP_TR_ACTIVE, 0x0E);
    if (err) return;
    switch (val) {
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
    static const uint8_t gvals[] = { 0x00, 0x10, 0x14, 0x18, 0x1C, 0x48, 0x49 };
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
    if (!err) ESP_LOG_BUFFER_HEXDUMP(TAG, v + 1, l - 1, ESP_LOG_WARN);
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

// TODO: rewrite with SMBus APIs and remove dependency on revk/ESP32-VL53L0X
#if defined(CONFIG_BASE_VLX_SENSOR) && !__has_include("vl53l0x.h")
#   warning "Run `git clone git@github.com:revk/ESP32-VL53L0X`"
#   undef CONFIG_BASE_VLX_SENSOR
#endif

#ifdef CONFIG_BASE_VLX_SENSOR
#   include "vl53l0x.h"

static vl53l0x_t *vlx;

static void vlx_initialize() {
    uint8_t addr = 0x29;
    if (smbus_probe(NUM_I2C, addr)) return;
    vlx = vl53l0x_config(NUM_I2C, PIN_SCL, PIN_SDA, -1, addr, 0);
    const char *err = vl53l0x_init(vlx);
    if (err) {
        ESP_LOGE(TAG, "VLX: initialize VL53L0X failed: %s", err);
        TRYFREE(vlx, vl53l0x_end);
    }
}

uint16_t vlx_probe() { return vl53l0x_readRangeSingleMillimeters(vlx); }
#else
uint16_t vlx_probe() { return (uint16_t)-1; }
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
static const uint8_t i2c_als_addr[4] = {
    0b01000100, 0b01000101,         // east, west
    0b01000110, 0b01000111          // south, north
};

static void als_initialize() {
    esp_err_t err;
    uint8_t addr;
    uint16_t buf[2];
    LOOPN(i, LEN(i2c_als_addr)) {
        if (smbus_probe(NUM_I2C, addr = i2c_als_addr[i])) continue;
        if (
            ( err = smbus_read_word(NUM_I2C, addr, 0x7E, buf + 0) ) ||
            ( err = smbus_read_word(NUM_I2C, addr, 0x7F, buf + 1) )
        ) {
            ESP_LOGE(TAG, "ALS: read %d failed: %s", i, esp_err_to_name(err));
            continue;
        }
        ESP_LOGI(TAG, "ALS: found %c%c %04X at I2C %d-%02X",
                buf[0] >> 8, buf[0] & 0xFF, buf[1], NUM_I2C, addr);
        // 15:12 RN[3:0] = 1100b    automatic full-scale
        // 11    CT      = 1b       conversion time 800ms
        // 10:9  M[1:0]  = 11b      continuous mode
        // 8:5                      read-only
        // 4     L       = 1b       latched window-style
        // 3     POL     = 0b       INT pin reports active low
        // 2     ME      = 0b       mask exponent off
        // 1:0   FC[1:0] = 10b      four fault counts
        smbus_write_word(NUM_I2C, addr, 0x01, 0xCE12);
        smbus_write_word(NUM_I2C, addr, 0x02, 0x0800); // low-limit 20.48 lux
        smbus_write_word(NUM_I2C, addr, 0x03, 0xBFFF); // high-limit 83865 lux
    }
}

float als_brightness(int idx) {
    if (idx < 0 || idx > 3) {
        ESP_LOGE(TAG, "ALS: invalid chip index %d", idx);
        return 0;
    }
    uint16_t val;
    esp_err_t err;
    if (( err = smbus_read_word(NUM_I2C, i2c_als_addr[idx], 0x00, &val) )) {
        ESP_LOGW(TAG, "ALS: read %d failed: %s", idx, esp_err_to_name(err));
        return 0;
    }
    // Equation 3 at Page 20 of OPT3001 datasheet:
    //   lux = 0.01 * 2^E[3:0] * R[11:0]
    return 0.01 * (1 << (val >> 12)) * (val & 0xFFF);
}

esp_err_t als_tracking(als_track_t idx, int *hdeg, int *vdeg) {
    esp_err_t err;
    float bmax = 0, bmin = 1e10, btmp[4];
    if (idx <= ALS_TRACK_3) {                   // maximize brightness
        for (int i = 0, v = 0; v < 90; i++, v += 6) {
            for (int h = 0; h < 180; h += 5) {
                int htmp = i % 2 ? (180 - h) : h; // S line scanning
                if (( err = pwm_set_degree(htmp, v) )) return err;
                msleep(50);
                btmp[0] = als_brightness(idx);
                printf("ALS: H %3d V %3d %8.2f lux\n", htmp, v, btmp[0]);
                if (btmp[0] > bmax) {
                    bmax = btmp[0];
                    *hdeg = htmp;
                    *vdeg = v;
                }
            }
        }
    } else if (idx == ALS_TRACK_H) {    // minimize diff of east & west
        for (int h = 0; h < 180; h += 15) {
            if (( err = pwm_set_degree(h, -1) )) return err;
            msleep(200);
            if (( btmp[0] = als_brightness(0) ) > bmax) bmax = btmp[0];
            if (( btmp[1] = als_brightness(1) ) > bmax) bmax = btmp[1];
            if (( btmp[2] = ABSDIFF(btmp[0], btmp[1]) ) < bmin) {
                bmin = btmp[2];
                *hdeg = h;
            }
        }
    } else if (idx == ALS_TRACK_V) {    // minimize diff of north & south
        for (int v = 0; v < 90; v += 9) {
            if (( err = pwm_set_degree(-1, v) )) return err;
            msleep(200);
            if (( btmp[0] = als_brightness(2) ) > bmax) bmax = btmp[0];
            if (( btmp[1] = als_brightness(3) ) > bmax) bmax = btmp[1];
            if (( btmp[2] = ABSDIFF(btmp[0], btmp[1]) ) < bmin) {
                bmin = btmp[2];
                *vdeg = v;
            }
        }
    } else if (idx == ALS_TRACK_A) {
        puts("TODO: PID Light Tracking");
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
    static const char * INPTYPE[] = {
        "No input", "USB Host SDP", "USB CDP(1.5A)", "USB DCP(3.25A)",
        "ADJ DCP(1.5A)", "Unknown(0.5A)", "Non-standard(<2.4A)", "OTG"
    };
    static const char * CHGSTAT[] = {
        "Discharging", "Pre-charge", "Fast-charge", "Charge done"
    };
    static const char * CHGERR[] = {
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
