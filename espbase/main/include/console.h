/* 
 * File: console.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 15:57:35
 */

#pragma once

#include "globals.h"

#define ESP_ERR_CONSOLE_ARGPARSE 0xABCD

#define CONSOLE_SYS_RESTART         //  102 Bytes
#define CONSOLE_SYS_UPDATE          // 3196 Bytes
#define CONSOLE_SYS_SLEEP           //12292 Bytes
#define CONSOLE_SYS_EXEC            // 7684 Bytes

#define CONSOLE_UTIL_DATETIME       //  176 Bytes
#define CONSOLE_UTIL_VERSION        //  272 Bytes
#define CONSOLE_UTIL_LSHW           // 1988 Bytes
#define CONSOLE_UTIL_LSPART         //  808 Bytes
#define CONSOLE_UTIL_LSTASK         //  300 Bytes
#define CONSOLE_UTIL_LSMEM          // 1308 Bytes
#define CONSOLE_UTIL_LSFS           //  512 Bytes
#define CONSOLE_UTIL_CONFIG         // 2852 Bytes
#define CONSOLE_UTIL_LOGGING        //  596 Bytes
#define CONSOLE_UTIL_HISTORY        //  806 Bytes

#define CONSOLE_DRV_GPIO            //  700 Bytes
#define CONSOLE_DRV_USB             //  430 Bytes
#define CONSOLE_DRV_LED             //  700 Bytes
#define CONSOLE_DRV_I2C             // 1436 Bytes
#define CONSOLE_DRV_ADC             // 1424 Bytes
#define CONSOLE_DRV_DAC             // 1244 Bytes
#define CONSOLE_DRV_PWM             // 1668 Bytes

#define CONSOLE_NET_BT              //  368 Bytes
#define CONSOLE_NET_STA             //  428 Bytes
#define CONSOLE_NET_AP              // 3884 Bytes
#define CONSOLE_NET_FTM             // 1860 Bytes
#define CONSOLE_NET_MDNS            //  520 Bytes
#define CONSOLE_NET_SNTP            //  482 Bytes
#define CONSOLE_NET_PING            // 5132 Bytes
#define CONSOLE_NET_IPERF           // 7432 Bytes
#define CONSOLE_NET_TSYNC           // 7888 Bytes

#define CONSOLE_APP_HID             // 1584 Bytes
#define CONSOLE_APP_SCN             //  200 Bytes
#define CONSOLE_APP_ALS             // 1408 Bytes
#define CONSOLE_APP_AVC             //  800 Bytes
#define CONSOLE_APP_SEN             // 1872 Bytes

#ifdef __cplusplus
extern "C" {
#endif

// Config and init console. Commands are registered at the end.
void console_initialize();

void console_register_prompt(const char *str, const char *ctx);

char * console_handle_command(const char *cmd, bool pipe, bool history);

/* (R) Read from console stream
 * (E) parse and Execute the command by `console_handle_command`
 * (P) then Print the result
 */
void console_handle_one();

// (L) endless Loop of `console_handle_one`.
void console_handle_loop(void*);

// Create a FreeRTOS task of function `console_handle_loop`.
void console_loop_begin(int xCoreID);

// Light weight JSON RPC dispatcher: parse json -> execute -> pack result
char * console_handle_rpc(const char *json);

#ifdef __cplusplus
}
#endif
