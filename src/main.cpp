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
#define NUM_PIXELS 38
#define DEBOUNCE_TIME 50
#define COLOR_SAT 255

#define DROPPER_ID "0"
#define MQTT_SERVER "RoboCon-Arena.lan"

#define OPEN 0
#define CLOSED 1

#define STATE_BOOT 0
#define STATE_NET 1
#define STATE_MQTT 2

#define ANIM_STATE_IDLE 0
#define ANIM_STATE_ACTIVE 1
#define ANIM_STATE_END 2

// trigger cube load (door close)
#define MSG_LOAD 'l'
// trigger cube drop (door open)
#define MSG_DROP 'd'
// trigger gate toggle
#define MSG_TOGGLE 't'
// before game start
#define MSG_GAME_IDLE 'i'
// during game
#define MSG_GAME_ACTIVE 'a'
// game about to end
#define MSG_GAME_END 'e'

// common colors
RgbColor red(COLOR_SAT, 0, 0);
RgbColor green(0, COLOR_SAT, 0);
RgbColor blue(0, 0, COLOR_SAT);
RgbColor white(COLOR_SAT);
RgbColor black(0);  // off

Servo servo;
NeoPixelBus<NeoBrgFeature, NeoWs2811Method> strip(NUM_PIXELS);

int last_bounce_state = LOW;
int button_state = HIGH;
unsigned long last_debounce = 0;
int anim_state = ANIM_STATE_IDLE;
unsigned long anim_frame = 0;
short gate_state = OPEN;

WiFiClient espWiFiClient;
PubSubClient mqttClient(espWiFiClient);
WiFiUDP udp;
Resolver resolver(udp);
IPAddress mqttIp = INADDR_NONE;

void clear_leds() {
    for (int i = 0; i < NUM_PIXELS; i++) {
        strip.SetPixelColor(i, black);
    }

    strip.Show();
}

void drop_anim(bool reverse) {
    for (int step = 0; step <= 7; step++) {
        int sat = (COLOR_SAT + 1) >> (reverse ? step : (7 - step));
        RgbColor color(sat - 1, 0, sat - 1);

        for (int pix = 0; pix < NUM_PIXELS; pix++) {
            strip.SetPixelColor(pix, color);
        }
        strip.Show();

        delay(71);
    }
}

void load_anim_start() {
    clear_leds();

    // fade in initial blue colour
    for (int step = 0; step <= 7; step++) {
        int sat = (COLOR_SAT + 1) >> (7 - step);
        RgbColor color(0, 0, sat - 1);

        for (int pix = 0; pix < NUM_PIXELS; pix++) {
            strip.SetPixelColor(pix, color);
        }
        strip.Show();

        delay(42);
    }
}

void load_anim_end() {
    // trail that "eats" the previous blue colour
    for (int i = 0; i < NUM_PIXELS; i++) {
        // this fades to purple, although divs are slow
        uint16_t r = (i == 0 ? 0 : (uint16_t)(((float)i / (float)NUM_PIXELS) * (float)COLOR_SAT));
        RgbColor color(r, 0, COLOR_SAT);

        strip.SetPixelColor(i, color);

        if (i - 3 >= 0) {
            strip.SetPixelColor(i - 3, black);
        }

        strip.Show();
        delay(42);
    }

    for (int i = NUM_PIXELS - 4; i < NUM_PIXELS; i++) {
        strip.SetPixelColor(i, black);
    }
    strip.Show();
}

void anim() {
    // things in here run 50 times per second
    // don't add delays
    switch (anim_state) {
        case ANIM_STATE_IDLE:
            break;
        case ANIM_STATE_ACTIVE:
            break;
        case ANIM_STATE_END:
            {
                uint16_t sat = COLOR_SAT * abs(sin((float)anim_frame / (float)16));
                RgbColor color(sat, 0, 0);

                for (int i = 0; i < NUM_PIXELS; i++) {
                    strip.SetPixelColor(i, color);
                }

                strip.Show();
                break;
            }
    }

    anim_frame++;
}

void drop_cube() {
    Serial.println("dropping cube");

    drop_anim(false);

    servo.write(180);
    gate_state = OPEN;
    delay(1000);

    drop_anim(true);
    clear_leds();
}

void load_cube() {
    Serial.println("loading cube");

    load_anim_start();

    servo.write(0);
    gate_state = CLOSED;
    delay(1000);

    load_anim_end();
}

void toggle_gate() {
    if (gate_state == OPEN) {
        Serial.println("closing gate");
        servo.write(0);
        gate_state = CLOSED;
    } else {
        Serial.println("opening gate");
        servo.write(180);
        gate_state = OPEN;
    }

    delay(500);
}

void callback(char* topic, byte* payload, unsigned int length) {
    // we only subscribed to one topic, no need to check

    if (length < 1)
        return;

    // just use single chars for messages
    switch ((char)payload[0]) {
        case MSG_LOAD:
            load_cube();
            break;
        case MSG_DROP:
            drop_cube();
            break;
        case MSG_TOGGLE:
            toggle_gate();
            break;
        case MSG_GAME_IDLE:
            anim_state = ANIM_STATE_IDLE;
            anim_frame = 0;
            Serial.println("idle game state");
            break;
        case MSG_GAME_ACTIVE:
            anim_state = ANIM_STATE_ACTIVE;
            anim_frame = 0;
            Serial.println("active game state");
            break;
        case MSG_GAME_END:
            anim_state = ANIM_STATE_END;
            anim_frame = 0;
            Serial.println("end game state");
            break;
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
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print("wifi ");
    Serial.println(WIFI_SSID);

    // broadcast mdns name `dropperX`
    String name = "dropper" + String(DROPPER_ID);
    MDNS.begin(name.c_str());
}

void mqtt_resolve() {
    resolver.setLocalIP(WiFi.localIP());
    mqttIp = resolver.search(MQTT_SERVER);
}

void mqtt_reconnect() {
    Serial.print("mqtt broker @ ");
    Serial.println(MQTT_SERVER);
    Serial.println(", ");
    Serial.println(mqttIp);

    String clientId = "dropper" + String(DROPPER_ID);

    if (mqttClient.connect(clientId.c_str())) {
        String topic = "dropper/" + String(DROPPER_ID);
        mqttClient.subscribe(topic.c_str());
    } else {
        Serial.println("mqtt failed");
    }
}

void setup() {
    Serial.begin(9600);

    strip.Begin();
    clear_leds();

    servo.attach(SERVO_PIN);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // open servo initially for cube load
    servo.write(180);
    gate_state = OPEN;
    delay(1000);
}

void _loop() {
    // network checks

    set_net_led_state(STATE_BOOT);

    if (WiFi.status() != WL_CONNECTED) {
        wifi_reconnect();
        return;
    }

    set_net_led_state(STATE_NET);

    if (mqttIp == INADDR_NONE) {
        mqtt_resolve();
        return;
    }

    mqttClient.setServer(mqttIp, 1883);
    mqttClient.setCallback(callback);

    if (!mqttClient.connected()) {
        mqtt_reconnect();
        return;
    }

    set_net_led_state(STATE_MQTT);

    // network is good, continue as normal

    anim();

    // button debounced to avoid false presses
    button_state = digitalRead(BUTTON_PIN);
    if (button_state != last_bounce_state) {
        last_debounce = millis();
        last_bounce_state = button_state;
    }

    if ((millis() - last_debounce) > DEBOUNCE_TIME && button_state == LOW) {
        Serial.println("load button pressed");
        toggle_gate();
    }

    // 50 iter/sec
    delay(20);
}

void loop() {
    _loop();
    mqttClient.loop();
    resolver.loop();
}
