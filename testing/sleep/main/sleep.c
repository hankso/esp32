// #include <Arduino.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "commands.h"

#define BUTTON_WAKE CONFIG_WAKEUP_GPIO
#define BUTTON_WAKE_LEVEL 0

#define UART_NUM UART_NUM_0

const char* prompt = "testing> ";


void initialize_uart() {
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .use_ref_tick = true
    };
    ESP_ERROR_CHECK( uart_param_config(UART_NUM, &uart_config) );
    ESP_ERROR_CHECK( uart_driver_install(UART_NUM, 256, 0, 0, NULL, 0) );
    esp_vfs_dev_uart_use_driver(UART_NUM);
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
}

void initialize_console() {
    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 8,
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN),
        .hint_bold = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    esp_console_register_help_command();

    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*)&esp_console_get_hint);
    linenoiseHistorySetMaxLen(50);
    if (linenoiseProbe() != 0) {
        linenoiseSetDumbMode(1);
    } else {
#if CONFIG_LOG_COLORS
        prompt = LOG_COLOR_I "testing> " LOG_RESET_COLOR;
#endif
    }

    register_version();
}

void initialize_sleep() {
    gpio_config_t button_conf = {
        .pin_bit_mask = BIT64(BUTTON_WAKE),
        .mode = GPIO_MODE_INPUT,
        // .pull_up_en = GPIO_PULLUP_ENABLE,
        // .pull_down_en = GPIO_PULLDOWN_DISABLE,
        // .intr_type = GPIO_INTR_LOW_LEVEL,
    };
    ESP_ERROR_CHECK(gpio_config(&button_conf));
    gpio_wakeup_enable((gpio_num_t)BUTTON_WAKE, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

void setup() {
    initialize_uart();
    initialize_console();
    initialize_sleep();
}

void loop() {
    char* line = linenoise(prompt);
    // printf("Received command: %s\n", line);
    if (line == NULL) return;
    linenoiseHistoryAdd(line);
    int cbret;
    esp_err_t err = esp_console_run(line, &cbret);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("Unrecognized command: %s\n", line);
    } else if (err == ESP_OK && cbret != ESP_OK) {
        printf("Command returned error code: 0x%d name: %s\n",
                cbret, esp_err_to_name(cbret));
    } else if (err != ESP_OK) {
        printf("%s\n", esp_err_to_name(err));
    }
    linenoiseFree(line);
}

void app_main() {
    setup();
    while (true) {
        loop();
    }
}
