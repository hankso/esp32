/*************************************************************************
File: webserver.h
Author: Hankso
Webpage: http://github.com/hankso
Time: Mon 27 May 2019 15:29:05 CST
************************************************************************/

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

#if defined(CONFIG_FS_FFat)
    #include <FFat.h>
    #define FFS FFat
// #elif defined(CONFIG_FS_SPIFFS)
#else
    #include <SPIFFS.h>
    #define FFS SPIFFS
#endif

#include "mimetable.h"
#include "globals.h"


AsyncWebServer server(80);

String jsonify_temp() {
    String msg = "[";
    for (int i = 0; i < 6; i++) {
        msg += (i ? "," : "") + String(temp_value[i]);
    }
    msg += "]";
    return msg;
}

String jsonify_dir(File dir) {
    String path, type, msg = "";
    File file;
    while (file = dir.openNextFile()) {
        path = file.name();
        path = path.substring(path.lastIndexOf('/') + 1);
        type = file.isDirectory() ? "dir" : "file";
        msg += 
            String(msg.length() ? "," : "") + 
            "{"
                "\"name\":\"" + path + "\","
                "\"type\":\"" + type + "\","
                "\"time\":" + String(file.getLastWrite()) + ""
            "}";
    }
    file.close();
    return "[" + msg + "]";
}

void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    LIGHTON();
    if (!index) {
        Serial.printf("Updating file: %s\n", filename.c_str());
        // Update.runAsync(true);
        if (!Update.begin()) {
            Update.printError(Serial);
        }
    }
    if (!Update.hasError()) {
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
        }
    }
    if (final) {
        if (Update.end(true)) {
            Serial.printf("Update Success: %.2f kB\n", (index + len) / 1024.0);
        } else {
            Update.printError(Serial);
        }
        Serial.flush();
    }
    LIGHTOFF();
}

void handleList(AsyncWebServerRequest *request) {
    LIGHTON();
    log_d("Get: %s", request->url().c_str());
    String path = request->hasArg("dir") ? request->arg("dir") : "/";
    if (!path.startsWith("/")) {
        path = '/' + path;
    }
    File root = FFS.open(path);
    if (!root) {
        request->send(404, "Dir does not exists.");
    } else if (!root.isDirectory()) {
        request->redirect(path);
    } else {
        request->send(200, "application/json", jsonify_dir(root));
    }
    root.close();
    LIGHTOFF();

}

void handleCreate(AsyncWebServerRequest *request){
    // handle file|dir create
    log_d("Get: %s", request->url().c_str());
    if (!request->hasArg("name")) {
        return request->send(400, "text/plain", "No filename specified.");
    }
    String 
        path = request->arg("name"),
        type = request->hasArg("type") ? request->arg("type") : "file";
    if (type == "file") {
        if (FFS.exists(path)) 
            return request->send(403, "text/plain", "File already exists.");
        File file = FFS.open(path, "w");
        if (file) {
            file.close();
        } else {
            return request->send(500, "text/plain", "Create failed.");
        }
    } else if (type == "dir") {
        File dir = FFS.open(path);
        if (dir.isDirectory()) {
            dir.close();
            return request->send(403, "text/plain", "Dir already exists.");
        }
        if (!FFS.mkdir(path)) {
            return request->send(500, "text/plain", "Create failed.");
        }
    }
    request->send(200);
}

void handleDelete(AsyncWebServerRequest *request){
    // handle file|dir delete
    log_d("Get: %s", request->url().c_str());
    if (!request->hasArg("name")) {
        return request->send(400, "text/plain", "No filename specified.");
    }
    String 
        path = request->arg("name"),
        type = request->hasArg("type") ? request->arg("type") : "file";
    if (type == "file") {
        if (!FFS.exists(path))
            return request->send(403, "text/plain", "File does not exist.");
        if (!FFS.remove(path)) {
            return request->send(500, "text/plain", "Delete failed.");
        }
    } else if (type == "dir") {
        File dir = FFS.open(path);
        if (!dir) {
            dir.close();
            return request->send(403, "text/plain", "Dir does not exist.");
        }
        if (!FFS.rmdir(path)) {
            return request->send(500, "text/plain", "Delete failed.");
        }
    }
    request->send(200);
}
    
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    // handle file upload
    LIGHTON();
    static File file;
    if (!index) {
        Serial.printf("Uploading file: %s\n", filename.c_str());
        if (!filename.startsWith("/")) filename = "/src/" + filename;
        if (FFS.exists(filename)) {
            if (request->hasArg("overwrite")) {
                FFS.remove(filename);
            } else {
                LIGHTOFF();
                return request->send(403, "text/plain", "File already exists.");
            }
        }
        file = FFS.open(filename, "w");
    }
    file.write(data, len);
    if (final) {
        if (file) {
            file.flush();
            file.close();
        } else {
            LIGHTOFF();
            return request->send(403, "Upload failed.");
        }
    }
    LIGHTOFF();
}

void webserver_init() {
    FFS.begin();

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        log_d("Get: %s", request->url().c_str());
        request->redirect("/index.html");
    });

    server.on("/temp", HTTP_GET, [](AsyncWebServerRequest *request){
        LIGHTON();
        log_d("Get: %s", request->url().c_str());
        request->send(200, "application/json", jsonify_temp());
        LIGHTOFF();
    });
    
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        log_d("Get: %s", request->url().c_str());
        request->send(200, "text/html", UPDATE_HTML);
    });
    
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
        bool reboot = !Update.hasError();
        request->send(200, "text/plain", reboot ? "OK" : "FAIL");
        if (reboot) ESP.restart();
    }, handleUpdate);
    
    server.on("/list", HTTP_GET, handleList);

    server.on("/edit", HTTP_PUT, handleCreate);
    server.on("/edit", HTTP_DELETE, handleDelete);
    server.on("/edit", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Uploaded");
    }, handleUpload);
    
    server.serveStatic("/", FFS, "/src/").setDefaultFile("non-exist");

    server.onNotFound([](AsyncWebServerRequest *request){
        String path = request->url();
        log_d("Get: %s", path.c_str());
        File file = FFS.open(path, "r");
        if (!file || !file.isDirectory()) {
            file.close();
            return request->send(404, "text/html", ERROR_HTML);
        }
        log_d("Get: %s is directory, goto file manager.", path.c_str());
        return request->send(200, "text/html", FILES_HTML0 + path + FILES_HTML1);
    });

    server.begin();
}

#endif // WEBSERVER_H
