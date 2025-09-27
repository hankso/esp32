/*
 * File: sensors.h
 * Authors: Hank <hankso1106@gmail.com>
 * Time: 2024/4/29 21:45:52
 */

#pragma once

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

void sensors_initialize();

float temp_celsius();       // degC

typedef struct {
    uint8_t ges;
#   define GES_NONE     0
#   define GES_MOVE_UP  1
#   define GES_MOVE_RT  2
#   define GES_MOVE_DN  3
#   define GES_MOVE_LT  4
#   define GES_ZOOM_IN  5
#   define GES_ZOOM_OT  6
    uint8_t num;            // 0-16, fingers number
    struct {
        uint8_t id : 4;     // 0-15, finger index
        uint8_t evt : 2;    // 0: press, 1: release, 2: contact
        uint8_t wt;         // 0-255, touch pressure
        uint8_t area : 4;   // 0-15, touch area
        uint16_t x, y;      // position in pixel
        uint16_t px, py;    // position percentage [0, 10000]
    } PACKED pts[16];
    bool applied;           // rotation and selection applied: not raw data
} PACKED tscn_data_t;

esp_err_t tscn_read(tscn_data_t *, bool apply);
esp_err_t tscn_command(const char *ctrl);   // start / stop / stat tscn task
void tscn_print(tscn_data_t *, FILE *, bool newline);

esp_err_t mscn_status();    // IST3931

int vlx_read();             // mm

typedef struct {
    float brightness;       // lux
    float temperature;      // degC
    float atmosphere;       // Pa
    float humidity;         // 0-1 percentage
    float altitude;         // meter
} gy39_data_t;

esp_err_t gy39_read(gy39_data_t *);

typedef enum {
#   define ALS_NUM 4
    ALS_TRACK_0,            // single input
    ALS_TRACK_1,
    ALS_TRACK_2,
    ALS_TRACK_3,
    ALS_TRACK_H,            // dual input
    ALS_TRACK_V,
    ALS_TRACK_A,            // quad input
} als_track_t;

float als_brightness(uint8_t idx); // lux, idx < ALS_NUM

esp_err_t als_tracking(als_track_t method, int *hdeg, int *vdeg);

esp_err_t pwr_status();     // BQ25895

#ifdef __cplusplus
}
#endif
