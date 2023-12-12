#include "Arduino.h"
#include "SimpleBLE.h"

#define LED 2

void setup() {
    Serial.begin(115200);
    Serial.println("Hello world!");
    pinMode(LED, OUTPUT);
}

void loop() {
    delay(1000);
    digitalWrite(LED, HIGH);
    delay(1000);
    digitalWrite(LED, LOW);
}
