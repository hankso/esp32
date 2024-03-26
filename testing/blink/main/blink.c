/*************************************************************************
File: ${ESP_PATH}/projects/testing/blink/main/blink.c
Author: Hankso
Page: http://github.com/hankso
Time: Fri 25 Jan 2019 15:09:44 CST
************************************************************************/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "tinyusb.h"

#define LED CONFIG_BLINK_GPIO

void app_main() {
    // tinyusb_driver_install(NULL);
    printf("Hello world!\n"); fflush(stdout);

    gpio_pad_select_gpio(LED);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    while(1) {
        gpio_set_level(LED, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_set_level(LED, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
