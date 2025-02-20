/*
 * File: main.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-13 18:03:04
 */

#include "globals.h"
#include "drivers.h"
#include "console.h"
#include "filesys.h"
#include "network.h"
#include "sensors.h"
#include "usbmode.h"
#include "ledmode.h"
#include "screen.h"
#include "btmode.h"
#include "update.h"
#include "config.h"
#include "server.h"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void shutdown() { ESP_LOGW(Config.info.NAME, "Goodbye!"); }

void setup() {
    esp_register_shutdown_handler(shutdown);

    // 1. low level drivers
    config_initialize();
    driver_initialize();

    // 2. necessary modules
    update_initialize();
    filesys_initialize();
    console_initialize();

    // 3. external devices
    screen_initialize();
    sensors_initialize();

    // 4. optional modules
    network_initialize();
    usbmode_initialize();
    btmode_initialize();
    server_initialize();

    led_set_blink(0);
    console_loop_begin(1);  // run REPL on Core 1 (i.e. App CPU)
}

void loop() {
    twdt_feed();
    msleep(500);
}

#ifndef CONFIG_AUTOSTART_ARDUINO
void loopTask(void *pvParameters) {
    setup();
    for (;;) {
        loop();
    }
}

void app_main(void) {
    //                      function  task name  stacksize param prio hdlr cpu
    xTaskCreatePinnedToCore(loopTask, "mainloop", 1024 * 4, NULL, 1, NULL, 1);
}
#endif
