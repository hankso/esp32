/* 
 * File: server.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:29:05
 *
 * Static files:
 *  Name    Method  Description
 *  /data   GET     Serve static files from /flashfs/data/ folder
 *  /docs   GET     Serve static files from /flashfs/docs/ folder
 *  /       GET     Serve static files from /flashfs/www/ folder
 *
 * API list (for STA & AP mode):
 *  Name    Method  Description (for STA & AP mode)
 *  /ws     POST    Websocket connection point: messages are parsed as JSON
 *  /alive  GET     Just respond `200 OK`
 *  /exec   POST    Manually send in command string just like using console
 *                  - param `?cmd=str&gcode=str`
 *  /media  GET     Fetch or check multi-media (audio / video) streaming
 *                  - param `?video=<mjpg|config>&audo=<wav|config>`
 *  /media  POST    Update microphone or camera configs
 *                  - param `?video=config&audio=config`
 *
 * API list (for AP mode only and auth needed):
 *  Name    Method  Description
 *  /edit   GET     Online Editor page
 *                  - param `?path=str&list&download`
 *  /edit   PUT     Create file|dir
 *                  - param `?path=str&type=<file|dir>`
 *  /edit   DELETE  Delete file
 *                  - param `?path=str&type=<file|dir>&from=url`
 *  /edit   POST    Upload file
 *                  - param `?overwrite`
 *  /config GET     Get JSON string of configuration entries
 *  /config POST    Overwrite configuration options
 *                  - param `?json=str`
 *  /update GET     Updation guide page
 *                  - param `?raw`
 *  /update POST    Upload compiled binary firmware to OTA flash partition
 *                  - param `?reset&size=int`
 *  /apmode ANY     Test whether TCP client is connected from AP
 */

#pragma once

#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

void server_initialize();
void server_loop_begin();
void server_loop_end();

#ifdef __cplusplus
}
#endif
