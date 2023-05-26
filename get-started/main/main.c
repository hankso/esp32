/* Testing ESP-IDF by hank <hankso1106@gmail.com> */

#include "sdkconfig.h"
#include "globals.h"
#include "drivers.h"
#include "console.h"
#include "update.h"
#include "config.h"

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
    /* ESP_LOGI(TAG, "Init File Systems");         fs_initialize(); */
    /* ESP_LOGI(TAG, "Init WiFi Connection");      wifi_initialize(); */
    ESP_LOGI(TAG, "Init Hardware Drivers");     driver_initialize();
    ESP_LOGI(TAG, "Init Command Line Console"); console_initialize();
    fflush(stdout);
}

void setup() {
    scn_progbar(0);
    console_loop_begin(1);
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
    tick_next += 300 * portTICK_PERIOD_MS;

    twdt_feed();
    led_set_light(0, led_get_light(0) ? 0 : 1);
    scn_progbar((count += 2) / 255.0 * 100); // draw on screen
}

#if !CONFIG_AUTOSTART_ARDUINO
void loopTask(void *pvParameters) {
    for (;;) {
        loop();
    }
}

void app_main(void) {
    init(); setup();
    //                      function   task name  stacksize param prio hdlr cpu
    xTaskCreatePinnedToCore(loopTask, "main-loop", 1024 * 4, NULL, 1, NULL, 1);
}
#endif // CONFIG_AUTOSTART_ARDUINO
