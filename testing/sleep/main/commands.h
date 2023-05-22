#include "esp_console.h"

int get_version(int argc, char **argv) {
    printf("\nVersion: 1.0\n");
    return 0;
}

void register_version() {
    esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of firmware",
        .hint = NULL,
        .func = &get_version,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
