/*
 * File: main.c
 * Authors: Hank <hankso1106@gmail.com>
 * Create: 2020-03-13 18:03:04
 */

#include "globals.h"
#include "config.h"
#include "drivers.h"
#include "filesys.h"
#include "console.h"
#include "network.h"
#include "update.h"
#include "sensors.h"
#include "screen.h"
#include "usbmode.h"
#include "btmode.h"
#include "server.h"
#include "ledmode.h"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void shutdown() { ESP_LOGW(Config.info.NAME, "Goodbye!"); }

void app_main(void) {
    esp_register_shutdown_handler(shutdown);

    // 1. low level drivers
    config_initialize();
    driver_initialize();

    // 2. necessary modules
    filesys_initialize();
    console_initialize();
    network_initialize();
    update_initialize();

    // 3. external devices
    sensors_initialize();
    screen_initialize();

    // 4. optional modules
    usbmode_initialize();
    btmode_initialize();
    server_initialize();

    led_set_blink(0);
#ifdef CONFIG_BASE_DEBUG
    console_loop_begin(1);      // run REPL on Core 1 and stop maintask
#else
    console_handle_loop(NULL);  // run REPL on CONFIG_ESP_MAIN_TASK_AFFINITY
#endif
}
