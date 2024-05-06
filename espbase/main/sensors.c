/*
 * File: sensors.c
 * Author: Hankso
 * Webpage: http://github.com/hankso
 * Time: 2024/4/29 21:47:55
 */

#include "sensors.h"
#include "drivers.h"

static const char *TAG = "Sensors";

/******************************************************************************
 * Internal temperature sensor
 */

#ifdef SOC_TEMP_SENSOR_SUPPORTED
#   include "driver/temp_sensor.h"

static void temp_initialize() {
    temp_sensor_config_t temp_conf = TSENS_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( temp_sensor_set_config(temp_conf) );
    ESP_ERROR_CHECK( temp_sensor_start() );
    ESP_LOGI(TAG, "Temperature is %.3f Celsius degree", temp_celsius());
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
 * Distance Measurement
 */

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
        vl53l0x_end(vlx);
        vlx = NULL;
    }
}

uint16_t vlx_probe() {
    TickType_t tick_start = xTaskGetTickCount();
    uint16_t result_mm = vl53l0x_readRangeSingleMillimeters(vlx);
    int took_ms = ((int)xTaskGetTickCount() - tick_start) * portTICK_PERIOD_MS;
    if (result_mm != (uint16_t)-1) {
        printf("VLX: range %u mm took %d ms", result_mm, took_ms);
    } else {
        printf("VLX: failed to measure range");
    }
    return result_mm;
}
#else
uint16_t vlx_probe() { return 0; }
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
 * Sensor API
 */

void sensors_initialize() {
#ifdef CONFIG_BASE_ALS_TRACK
    als_initialize();
#endif
#ifdef CONFIG_BASE_VLX_SENSOR
    vlx_initialize();
#endif
#ifdef SOC_TEMP_SENSOR_SUPPORTED
    temp_initialize();
#endif
#ifdef CONFIG_BASE_USE_TPAD
    tpad_initialize();
#endif
}
