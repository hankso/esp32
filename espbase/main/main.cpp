/*
 * File: main.cpp
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-13 18:03:04
 */

#include "sdkconfig.h"
#include "globals.h"
#include "drivers.h"
#include "console.h"
#include "filesys.h"
#include "network.h"
#include "usbmode.h"
#include "update.h"
#include "config.h"
#include "server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

// Task list:
//  WiFi/AsyncTCP/WebServer Core 0
//  Console (command REPL) Core 1
//  Main loop (screen) Core 1

void setup() {
    config_initialize();
    update_initialize();
    filesys_initialize();
    network_initialize();
    driver_initialize();
    console_initialize();
    usbmode_initialize();

    led_set_blink(0);
    server_loop_begin();    // Core 0 (i.e. Pro CPU)
    console_loop_begin(1);  // Core 1 (i.e. App CPU)
}

void loop() {
    static uint8_t count = 0;
    asleep(500);
    scn_progbar((count += 2) / 255.0 * 100);
    twdt_feed();
}

#if !CONFIG_AUTOSTART_ARDUINO
void loopTask(void *pvParameters) {
    setup();
    for (;;) {
        loop();
    }
}

extern "C" void app_main(void) {
    //                      function  task name  stacksize param prio hdlr cpu
    xTaskCreatePinnedToCore(loopTask, "mainloop", 1024 * 4, NULL, 1, NULL, 1);
}
#endif // CONFIG_AUTOSTART_ARDUINO
