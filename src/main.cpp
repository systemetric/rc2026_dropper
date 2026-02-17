#include <Arduino.h>
#include <NeoPixelBus.h>
#include <Servo.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <mDNSResolver.h>
#include <PubSubClient.h>

using namespace mDNSResolver;

// located at include/secret.h
// defines WIFI_SSID and WIFI_PASS
#include "secret.h"

#define SERVO_PIN 13
#define BUTTON_PIN 5
#define LED_PIN 1
#define NUM_PIXELS 100
#define DEBOUNCE_TIME 50
#define COLOR_SAT 128

#define DROPPER_ID "0"
#define MQTT_SERVER "Wills-ThinkPad.local"

#define STATE_BOOT 0
#define STATE_NET 1
#define STATE_MQTT 2

RgbColor red(COLOR_SAT, 0, 0);
RgbColor green(0, COLOR_SAT, 0);
RgbColor blue(0, 0, COLOR_SAT);
RgbColor white(COLOR_SAT);
RgbColor black(0);

Servo servo;
NeoPixelBus<NeoBrgFeature, NeoWs2811Method> strip(NUM_PIXELS);

int last_bounce_state = LOW;
int button_state = HIGH;
unsigned long last_debounce = 0;

WiFiClient espWiFiClient;
PubSubClient mqttClient(espWiFiClient);
WiFiUDP udp;
Resolver resolver(udp);

void drop_anim(bool reverse) {
    for (int step = 0; step <= 7; step++) {
        int sat = 256 >> (reverse ? step : (7 - step));

        RgbColor color(sat - 1, 0, sat - 1);

        for (int pix = 0; pix < NUM_PIXELS; pix++) {
            strip.SetPixelColor(pix, color);
        }
        strip.Show();

        delay(71);
    }
}

void default_anim() {
    /*
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (button_state == HIGH) {
            strip.SetPixelColor(i, red);
        } else {
            strip.SetPixelColor(i, green);
        }
    }

    strip.Show();*/
}

void clear_leds() {
    for (int i = 0; i < NUM_PIXELS; i++) {
        strip.SetPixelColor(i, black);
    }

    strip.Show();
}

void drop_servo() {
    drop_anim(false);
    servo.write(180);
    delay(1000);
    drop_anim(true);
    clear_leds();
}

void load_servo() {
    servo.write(0);
    delay(1000);
}

void callback(char* topic, byte* payload, unsigned int length) {
    // we only subscribed to one topic, no need to check

    if (length < 1)
        return;

    if ((char)payload[0] == '1') {
        drop_servo();
    } else if ((char)payload[0] == '0') {
        load_servo();
    }
}

void set_net_led_state(int state) {
    switch (state) {
        case STATE_BOOT:
            strip.SetPixelColor(0, red);
            break;
        case STATE_NET:
            strip.SetPixelColor(0, blue);
            break;
        case STATE_MQTT:
            strip.SetPixelColor(0, green);
            break;
    }

    strip.Show();
}

void wifi_reconnect() {

    set_net_led_state(STATE_BOOT);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }

    set_net_led_state(STATE_NET);
}

void mqtt_reconnect() {
    set_net_led_state(STATE_NET);

    Serial.println("START MQTT");

    while (!mqttClient.connected()) {
        String clientId = "RC2026_DROPPER_" + String(DROPPER_ID);

        if (mqttClient.connect(clientId.c_str())) {
            String topic = "dropper/" + String(DROPPER_ID);
            mqttClient.subscribe(topic.c_str());
        } else {
            // connection failed, wait 2s
            delay(2000);
        }
    }

    Serial.println("GOT MQTT");

    set_net_led_state(STATE_MQTT);
}

void setup() {
    Serial.begin(9600);

    strip.Begin();
    clear_leds();
    strip.Show();

    servo.attach(SERVO_PIN);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifi_reconnect();
    Serial.println("GOT WIFI");

    String name = "dropper" + String(DROPPER_ID);
    MDNS.begin(name.c_str());
    Serial.println("GOT MDNS");

    // mqtt host uses mdns, resolve to ip
    Serial.println(MQTT_SERVER);
    resolver.setLocalIP(WiFi.localIP());
    IPAddress mqttIp;
    do {
        mqttIp = resolver.search(MQTT_SERVER);
    } while (mqttIp == INADDR_NONE);

    Serial.println(mqttIp);
    mqttClient.setServer(mqttIp, 1883);
    mqttClient.setCallback(callback);

    load_servo();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        wifi_reconnect();
    }

    if (!mqttClient.connected()) {
        mqtt_reconnect();
    }

    mqttClient.loop();
    resolver.loop();

    default_anim();

    button_state = digitalRead(BUTTON_PIN);
    if (button_state != last_bounce_state) {
        last_debounce = millis();
        last_bounce_state = button_state;
    }

    if ((millis() - last_debounce) > DEBOUNCE_TIME && button_state == LOW) {
        load_servo();
    }

    // 50 iter/sec
    delay(20);
}
