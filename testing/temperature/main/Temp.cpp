/*************************************************************************
File: ${ESP_BASE}/projects/Temprature/main/Temp.cpp
Author: Hankso
Time: Tue 21 May 2019 09:46:44 CST
************************************************************************/

#include <WiFi.h>

// local lib
#include "globals.h"
#include "webserver.h"
#include "spi_max6675.h"


void blink_test() {
    // Chip is not dead: light LED 300ms
    LIGHTON();  delay(300);
    LIGHTOFF(); delay(200);
}

void wifi_init() {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(
        IPAddress(192, 168, 1, 1),
        IPAddress(192, 168, 1, 1),
        IPAddress(255, 255, 255, 0)
    );
    WiFi.softAP(BSSID, PASSWD);
    Serial.printf("You can connect to WiFi hotspot `%s`.\n", BSSID);
    Serial.printf("Then visit http://");
    Serial.print(WiFi.softAPIP());
    Serial.println(" to look temprature values");
}

void setup() {
    pinMode(PIN_LED, OUTPUT);
    Serial.begin(115200);
    wifi_init();
    spi_max6675_init();
    webserver_init();
}

void loop() {
#ifdef CONFIG_DEBUG
    blink_test();
#endif
    spi_max6675_read();
    // Serial.print("Temprature: ");
    // for (int i = 0; i < 6; i++) {
    //     Serial.print("Sensor");
    //     Serial.print(i);
    //     Serial.print(": ");
    //     Serial.print(temp_value[i]);
    //     Serial.print(", ");
    // }
    // Serial.println("");
    delay(1000);
}
