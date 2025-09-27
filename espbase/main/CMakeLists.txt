idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "include"
    REQUIRES
        app_update
        bootloader_support
        console
        esp_adc
        esp_eth
        esp_hid
        esp_http_client
        esp_http_server
        esp_lcd
        esp_partition
        esp_psram
        esp_vfs_console
        fatfs
        json
        nvs_flash
        sdmmc
        spi_flash
        spiffs
        touch_element
        u8g2
        usb
        wear_levelling
)
