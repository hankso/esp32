/*************************************************************************
File: globals.h
Author: Hankso
Webpage: http://github.com/hankso
Time: Mon 27 May 2019 15:51:08 CST
************************************************************************/

#ifndef _globals_h_
#define _globals_h_

#include <Arduino.h>

#define PIN_LED CONFIG_BLINK_GPIO
#define LIGHTON() digitalWrite(PIN_LED, HIGH)
#define LIGHTOFF() digitalWrite(PIN_LED, LOW)

float temp_value[6];

const char
    *HOST = "esp32.tmpctl.net",  // URL server name
    *BSSID = "ESP32TempCtlAP",  // hotspot name
    *PASSWD = NULL;  // hotspot password

const char *ERROR_HTML = 
    "<html>"
        "<head>"
            "<script src='/js/404page.js'></script>"
        "</head>"
        "<body><h1>404: File not found.</h1></body>"
    "</html>";

const char *FILES_HTML0 = 
    "<html>"
        "<head>"
            "<meta charset='utf-8'>"
            "<title>";

const char *FILES_HTML1 = 
            "</title>"
            "<script src='/js/file-manager.js'></script>"
        "</head>"
        "<body>"
            "<div id='header'></div>"
            "<table>"
                "<thead>"
                    "<tr>"
                        "<th onclick='sortby(0)'>Name</th>"
                        "<th onclick='sortby(1)'>Size</th>"
                        "<th onclick='sortby(2)'>Date Modified</th>"
                    "</tr>"
                "</thead>"
                "<tbody><tbody>"
            "</table>"
        "</body>"
    "</html>";

const char *UPDATE_HTML = 
    "<form action='/update' method='post' enctype='multipart/form-data'>"
        "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>";

#endif // _globals_h_
