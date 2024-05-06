/*
 * File: sensors.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/29 21:45:52
 */

#pragma once

#include "globals.h"

#if defined(CONFIG_BASE_VLX_SENSOR) && !__has_include("vl53l0x.h")
#   warning "Run `git clone git@github.com:revk/ESP32-VL53L0X`"
#   undef CONFIG_BASE_VLX_SENSOR
#endif

#ifdef __cplusplus
extern "C" {
#endif

void sensors_initialize();

float temp_celsius();
uint16_t tpad_read();
uint16_t vlx_probe();

typedef struct {
    float brightness;   // lux
    float temperature;  // Celsius degree
    float atmosphere;   // Pa
    float humidity;     // 0-1 percentage
    float altitude;     // meter
} gy39_data_t;

esp_err_t gy39_measure(gy39_data_t *dat);

float als_brightness(int idx);

typedef enum {
    ALS_TRACK_0,    // single input
    ALS_TRACK_1,
    ALS_TRACK_2,
    ALS_TRACK_3,
    ALS_TRACK_H,    // dual input
    ALS_TRACK_V,
    ALS_TRACK_A,    // quad input
} als_track_t;

esp_err_t als_tracking(als_track_t method, int *hdeg, int *vdeg);

#ifdef __cplusplus
}
#endif
