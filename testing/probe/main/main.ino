#include <Arduino.h>

/* #define LED   CONFIG_LED_GPIO */
#define LED   2
/* #define PROBE CONFIG_PROBE_GPIO */
#define PROBE 23

void setup() {
    pinMode(LED, OUTPUT);
    pinMode(PROBE, INPUT);
    Serial.begin(115200);
}

void loop() {
    delay(5);
    digitalWrite(LED, digitalRead(PROBE));
    // Serial.println(pulseIn(PROBE, HIGH, 500));
}
