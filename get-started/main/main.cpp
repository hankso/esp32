/* Testing ESP-IDF by hank <hankso1106@gmail.com> */

#include "sdkconfig.h"
#include "globals.h"
#include "drivers.h"
#include "console.h"
#include "filesys.h"
#include "network.h"
#include "update.h"
#include "config.h"
#include "server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "hankso";

// Task list:
//  WiFi/AsyncTCP/WebServer Core 0
//  Console (command REPL) Core 1
//  Main loop (screen) Core 1

void init() {
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "Init Configuration");        config_initialize();
    ESP_LOGI(TAG, "Init OTA Updation");         ota_initialize();
    ESP_LOGI(TAG, "Init File Systems");         fs_initialize();
    ESP_LOGI(TAG, "Init Network Connection");   network_initialize();   // 89040 Bytes
    ESP_LOGI(TAG, "Init Hardware Drivers");     driver_initialize();
    ESP_LOGI(TAG, "Init Command Line Console"); console_initialize();
    fflush(stdout);
}

void setup() {
    server_loop_begin();    // Core 0 (i.e. Pro CPU)
    console_loop_begin(1);  // Core 1 (i.e. App CPU)
#ifdef CONFIG_LED_INDICATOR
    led_set_blink(0);
#endif
}

void loop() {
    static TickType_t tick_curr = 0, tick_next = 0;
    static uint8_t count = 0;

    /* Accurate time control */
    tick_curr = xTaskGetTickCount();
    if (!tick_next) {
        tick_next = tick_curr;
    } else if (tick_curr < tick_next) {
        vTaskDelay(tick_next - tick_curr);
    }
    tick_next += 500 * portTICK_PERIOD_MS;
    twdt_feed();
    scn_progbar((count += 2) / 255.0 * 100); // draw on screen
}

#if !CONFIG_AUTOSTART_ARDUINO
void loopTask(void *pvParameters) {
    for (;;) {
        loop();
    }
}

extern "C" void app_main(void) {
    init(); setup();
    //                      function  task name  stacksize param prio hdlr cpu
    xTaskCreatePinnedToCore(loopTask, "mainloop", 1024 * 4, NULL, 1, NULL, 1);
}
#endif // CONFIG_AUTOSTART_ARDUINO
