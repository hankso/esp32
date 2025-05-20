idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "include"
    REQUIRES ESP32-VL53L0X app_update bootloader_support bt esp_adc_cal esp_hid esp_http_client esp_http_server esp_lcd fatfs iperf json libsodium mdns sdmmc spiffs tinyusb u8g2 usb wear_levelling
)
