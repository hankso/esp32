idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "include"
    REQUIRES
        app_update
        bootloader_support
        console
        esp32
        esp_adc_cal
        esp_hid
        esp_http_client
        esp_http_server
        esp_lcd
        fatfs
        json
        mdns
        nvs_flash
        sdmmc
        spi_flash
        spiffs
        u8g2
        usb
        wear_levelling
)
