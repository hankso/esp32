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
 *  /ws     POST    Websocket messages are regarded as JSON-RPC
 *  /alive  GET     Just respond `200 OK`
 *  /exec   POST    Run commands like using console REPL
 *                  - param `?cmd=str&gcode=str`
 *  /media  GET     Start or check audio / video streaming
 *                  - param `?video=<mjpg|config>&audo=<wav|config>`
 *  /media  POST    Config microphone or camera
 *                  - param `?video=config&audio=config`
 *
 * API list (for AP mode only and auth needed):
 *  Name    Method  Description
 *  /edit   GET     Online Editor page
 *                  - param `?path=str&list&download`
 *  /edit   PUT     Create file|dir
 *                  - param `?path=str&type=<file|dir>`
 *  /edit   DELETE  Delete file|dir
 *                  - param `?path=str&type=<file|dir>&from=url`
 *  /edit   POST    Upload file
 *                  - param `?overwrite`
 *  /config GET     Return configuration entries in JSON
 *  /config POST    Overwrite configuration entries
 *                  - param `?json=str`
 *  /update GET     Page of OTA updation
 *                  - param `?raw`
 *  /update POST    Upload compiled binary firmware to ESP32
 *                  - param `?reset&size=int`
 *  /apmode ANY     Test whether TCP client is connected from AP
 */

#pragma once

#include "globals.h"

#define CTYPE_HTML      "text/html"
#define CTYPE_TEXT      "text/plain"
#define CTYPE_JSON      "application/json"
#define CTYPE_UENC      "application/x-www-form-urlencoded"
#define CTYPE_MPRT      "multipart/form-data"
#define CTYPE_BDRY(x)   CTYPE_MPRT ";boundary=" STR(x)

#ifdef __cplusplus
extern "C" {
#endif

void server_initialize();
void server_loop_begin();
void server_loop_end();

#ifdef __cplusplus
}
#endif
