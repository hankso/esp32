/* 
 * File: update.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-23 11:42:30
 */

#pragma once

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

void update_initialize();

void ota_updation_reset();
bool ota_updation_begin(size_t);
bool ota_updation_write(uint8_t *, size_t);
bool ota_updation_end();
bool ota_updation_url(const char *);
bool ota_updation_partition(const char *);

const char * ota_updation_error();

void ota_partition_info();

#ifdef __cplusplus
}
#endif
