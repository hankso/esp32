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

bool ota_updation_url(const char *, bool);  // fetch firmware from URL
bool ota_updation_boot(const char *);       // set boot partition
void ota_updation_info();                   // print OTA status

void ota_updation_reset();                  // reset updation context

bool ota_updation_begin(size_t);            // accept OTA_SIZE_UNKNOWN
bool ota_updation_write(void *, size_t);
bool ota_updation_end();

const char * ota_updation_error();          // get current error string

#ifdef __cplusplus
}
#endif
