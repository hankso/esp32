/* 
 * File: console.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-12 15:57:35
 */

#pragma once

#include "globals.h"

#define ESP_ERR_CONSOLE_ARGPARSE 0xABCD

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
