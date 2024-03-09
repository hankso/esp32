/* 
 * File: server.h
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2019-05-27 15:29:05
 *
 * WebServerClass wraps on AsyncWebServer to provide only `begin` and `end`
 * function for easier usage.
 *
 * This component (AsyncServer framework & APIs) occupy about 113KB in firmware.
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

#ifdef __cplusplus
extern "C" {
#endif

void server_loop_begin();
void server_loop_end();

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

class WebServerClass {
private:
    AsyncWebServer _server = AsyncWebServer(80);
    AsyncWebSocket _wsocket = AsyncWebSocket("/ws");
    bool _started;
public:
    WebServerClass(): _started(false) {}
    ~WebServerClass() { _started = false; end(); }

    void begin();                   // run server in LWIP thread
    void end() { _server.end(); }   // stop AsyncWebServer
    void register_api_sta();
    void register_api_ap();
    void register_api_ws();
    void register_statics();
    bool logging();
    void logging(bool);             // enable/disable http request logging
};

extern WebServerClass WebServer;

#endif // __cplusplus
