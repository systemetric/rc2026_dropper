#include <Arduino.h>
#include <NeoPixelBus.h>
#include <Servo.h>

#define SERVO_PIN 13
#define BUTTON_PIN 5
#define LED_PIN 1
#define NUM_PIXELS 6
#define DEBOUNCE_TIME 50

#define COLOR_SAT 128

RgbColor red(COLOR_SAT, 0, 0);
RgbColor green(0, COLOR_SAT, 0);
RgbColor blue(0, 0, COLOR_SAT);
RgbColor white(COLOR_SAT);
RgbColor black(0);

const uint16_t PixelCount = 4;

Servo servo;
NeoPixelBus<NeoGrbFeature, NeoWs2811Method> strip(PixelCount);

int last_bounce_state = LOW;
int button_state = HIGH;
unsigned long last_debounce = 0;

void drop() {
    servo.write(180);
    delay(1000);
}

void load() {
    servo.write(0);
    delay(1000);
}

void anim() {
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (button_state == HIGH) {
            strip.SetPixelColor(i, red);
        } else {
            strip.SetPixelColor(i, green);
        }
    }

    strip.Show();
}

void setup() {
    strip.Begin();
    strip.Show();

    servo.attach(SERVO_PIN);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    load();
    drop();
}

void loop() {
    anim();

    button_state = digitalRead(BUTTON_PIN);
    if (button_state != last_bounce_state) {
        last_debounce = millis();
        last_bounce_state = button_state;
    }

    if ((millis() - last_debounce) > DEBOUNCE_TIME && button_state == LOW) {
        load();
    }

    // 50 iter/sec
    delay(20);
}
