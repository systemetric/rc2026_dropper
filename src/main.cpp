#include <Arduino.h>

const int LED_PIN = LED_BUILTIN;

void setup() {
    Serial.begin(9600);
    Serial.println("ESP-12E LED Blink Test");

    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED ON");
    delay(1000);

    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED OFF");
    delay(1000);
}
