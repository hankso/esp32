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

const char * ota_updation_error();          // get current error string

void ota_updation_reset();                  // reset updation context
bool ota_updation_begin(size_t);            // accept OTA_SIZE_UNKNOWN
bool ota_updation_write(void *, size_t);
bool ota_updation_end();

void ota_updation_info();                   // print OTA status
bool ota_updation_boot(const char *);       // set boot partition
bool ota_updation_url(const char *);        // fetch firmware from URL

#ifdef __cplusplus
}
#endif
