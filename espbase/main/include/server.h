/* 
 * File: server.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:29:05
 *
 * This feature (AsyncWebServer + APIs) occupies about 113KB in firmware.
 *
 * Static files:
 *  Name    Method  Description
 *  /data   GET     Serve static files from /flashfs/data/ folder
 *  /docs   GET     Serve static files from /flashfs/docs/ folder
 *  /       GET     Serve static files from /flashfs/www/ folder
 *
 * API list:
 *  Name    Method  Description (for STA & AP mode)
 *  /ws     POST    Websocket connection point: messages are parsed as JSON
 *  /cmd    POST    Manually send in command string just like using console
 *                  - param `?exec=str&gcode=str`
 *
 *  Name    Method  Description (for AP mode only and auth needed)
 *  /apmode ANY     Test whether TCP client is connected from AP
 *  /edit   GET     Online Editor page
 *                  - param `?list[=str]&path=str&download`
 *  /editc  ANY     Create file|dir (accept HTTP_PUT)
 *                  - param `?path=str&type=<file|dir>`
 *  /editd  ANY     Delete file (accept HTTP_DELETE)
 *                  - param `?path=str&from=url`
 *  /editu  POST    Upload file
 *                  - param `?overwrite`
 *  /config GET     Get JSON string of configuration entries
 *  /config POST    Overwrite configuration options
 *                  - param `?json=str`
 *  /update GET     Updation guide page
 *                  - param `?raw`
 *  /update POST    Upload compiled binary firmware to OTA flash partition
 *                  - param `?reset&size=int`
 */

#pragma once

#include "globals.h"

#if defined(CONFIG_USE_WEBSERVER) && !__has_include("ESPAsyncWebServer.h")
#   warning "Run `git clone git@github.com:me-no-dev/ESPAsyncWebServer`"
#   undef CONFIG_USE_WEBSERVER
#endif

#ifdef __cplusplus
extern "C" {
#endif

void server_loop_begin();
void server_loop_end();
bool server_get_logging();
void server_set_logging(bool);

#ifdef __cplusplus
}
#endif // __cplusplus
