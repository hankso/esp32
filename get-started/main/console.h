/* 
 * File: console.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 15:57:35
 */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_

#include <stdbool.h>

#define CONSOLE_SYSTEM_RESTART      // 102 Bytes
#define CONSOLE_SYSTEM_SLEEP        // 12292 Bytes
// #define CONSOLE_SYSTEM_UPDATE       // 3196 Bytes

#define CONSOLE_CONFIG_IO           // 2852 Bytes

#define CONSOLE_DRIVER_LED          // 700 Bytes
#define CONSOLE_DRIVER_GPIO         // 700 Bytes
#define CONSOLE_DRIVER_I2C          // 1436 Bytes
#define CONSOLE_DRIVER_ALS          // 1920 Bytes
#define CONSOLE_DRIVER_ADC          // 1424 Bytes
#define CONSOLE_DRIVER_PWM          // 224 Bytes

#define CONSOLE_UTILS_VER           // 272 Bytes
#define CONSOLE_UTILS_LSHW          // 1988 Bytes
#define CONSOLE_UTILS_LSCPU         // 300 Bytes
#define CONSOLE_UTILS_LSTASK        // 300 Bytes
// #define CONSOLE_UTILS_LSPART        // 808 Bytes
// #define CONSOLE_UTILS_LSMEM         // 1308 Bytes
// #define CONSOLE_UTILS_LSFS          // 512 Bytes
// #define CONSOLE_UTILS_HIST          // 806 Bytes

#define CONSOLE_NET_STA             // 428 Bytes
#define CONSOLE_NET_AP              // 3884 Bytes
// #define CONSOLE_NET_IPERF           // 24004 Bytes
// #define CONSOLE_NET_PING            // 21668 Bytes
// #define CONSOLE_NET_FTM             // 1860 Bytes

#ifdef __cplusplus
extern "C" {
#endif

// Config and init console. commands are registered at the end.
void console_initialize();

char * console_handle_command(const char *cmd, bool history);

/* (R) Read from console stream (wait until command input).
 * (E) parse and Execute the command by `console_handle_command`.
 * (P) then Print the result.
 */
void console_handle_one();

// (L) endless Loop of `console_handle_one`.
void console_handle_loop(void*);

// Create a FreeRTOS Task on function `console_handle_loop`.
void console_loop_begin(int xCoreID);

// Light weight JSON RPC dispatcher: parse json -> execute -> pack result
char * console_handle_rpc(const char *json);

#ifdef __cplusplus
}
#endif

#endif // _CONSOLE_H
