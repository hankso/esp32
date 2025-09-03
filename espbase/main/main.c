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
#include "ledmode.h"
#include "usbmode.h"
#include "btmode.h"
#include "screen.h"
#include "server.h"

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void shutdown() { ESP_LOGW(Config.info.NAME, "Goodbye!"); }

RTC_DATA_ATTR uint8_t count[3];

void app_main(void) {
    bool stop = false;
    switch (esp_reset_reason()) {
    case ESP_RST_PANIC:     stop = count[0]++ > 2; break;
    case ESP_RST_INT_WDT:   FALLTH;
    case ESP_RST_TASK_WDT:  FALLTH;
    case ESP_RST_WDT:       stop = count[1]++ > 5; break;
    case ESP_RST_POWERON:   FALLTH;
    case ESP_RST_BROWNOUT:  stop = count[2]++ > 5; break;
    case ESP_RST_DEEPSLEEP:
        ESP_LOGI(Config.info.NAME, "Wake from deep sleep"); break;
    default: memset(count, 0, sizeof(count));
    }
#ifdef CONFIG_BASE_DEBUG
    if (stop) {
        ESP_LOGE(Config.info.NAME, "Something wrong!");
        for (;;) { msleep(1000); }
    }
#endif
    esp_register_shutdown_handler(shutdown);

    // 1. low level drivers     // dependencies
    config_initialize();
    driver_initialize();        // button, knob, led_indicator, u8g2, lvgl, lcd

    // 2. necessary modules
    filesys_initialize();       // elf_loader
    console_initialize();
    network_initialize();       // iperf
    update_initialize();        // filesys, network

    // 3. optional modules
    sensors_initialize();       // drivers
    hidtool_initialize();       // filesys
    usbmode_initialize();       // hidtool, esp_tinyusb, usb_host_xxx
    btmode_initialize();        // hidtool
    server_initialize();        // network, update, filesys, console

    led_set_blink(0);
    scn_command(SCN_INIT, NULL);
    console_handle_loop(NULL);  // run REPL on CONFIG_ESP_MAIN_TASK_AFFINITY
}
