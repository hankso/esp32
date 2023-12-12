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
 * API list:
 *  Name    Method  Description
 *  /ws     POST    Websocket connection point: messages are parsed as JSON
 *  /cmd    POST    Manually send in command string just like using console
 *
 * Static files:
 *  Name    Method  Description
 *  /       GET     Serve static files from /flashfs/root/ folder
 *  /ap     GET     Serve static files from /flashfs/ap/ folder (auth needed)
 *  /sta    GET     Serve static files from /flashfs/sta/ folder
 *  /data   GET     Serve static files from /flashfs/data/ folder
 *  /assets GET     Serve static files from /flashfs/src/ folder
 *
 * SoftAP only:
 *  Name    Method  Description
 *  /config GET     Get JSON string of configuration entries
 *  /config POST    Overwrite configuration options
 *  /update GET     Updation guide page
 *  /update POST    Upload compiled binary firmware to OTA flash partition
 *  /edit   ANY     Online Editor page: create/delete/edit
 */

#ifndef _SERVER_H_
#define _SERVER_H_

void server_loop_begin();
void server_loop_end();

#ifdef __cplusplus

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

extern "C" {

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

}
#endif // __cplusplus

#endif // _SERVER_H_
