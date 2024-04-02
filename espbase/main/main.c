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
#include "usbmode.h"
#include "update.h"
#include "config.h"
#include "server.h"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void shutdown() { ESP_LOGW(Config.info.NAME, "Goodbye!"); }

void setup() {
    driver_initialize();
    config_initialize();
    update_initialize();
    filesys_initialize();
    usbmode_initialize();
    console_initialize();
    network_initialize();

    led_set_blink(0);
    server_loop_begin();    // Core 0 (i.e. Pro CPU)
    console_loop_begin(1);  // Core 1 (i.e. App CPU)
    esp_register_shutdown_handler(shutdown);
}

void loop() {
    twdt_feed();
    asleep(500);
}

#if !CONFIG_AUTOSTART_ARDUINO
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
#endif // CONFIG_AUTOSTART_ARDUINO
