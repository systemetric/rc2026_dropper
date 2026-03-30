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

#define DROPPER_ID "0"
#define HOSTNAME ("Dropper" DROPPER_ID)
#define ARENA_NAME "RoboCon-Arena.local"

#define OPEN 0
#define CLOSED 1

// common colors
RgbColor red(255, 0, 0);
RgbColor orange(255, 0, 125);
RgbColor yellow(255, 0, 192);
RgbColor notify_yellow(255, 0, 255);
RgbColor spring_green(125, 0, 255);
RgbColor green(0, 0, 255);
RgbColor teal(0, 128, 128);
RgbColor turquoise(0, 125, 255);
RgbColor cyan(0, 255, 255);
RgbColor ocean(0, 255, 128);
RgbColor blue(0, 128, 0); // was 256!
RgbColor violet(125, 255, 0);
RgbColor magenta(255, 255, 0);
RgbColor raspberry(255, 128, 0);
RgbColor white(255, 255, 255);
RgbColor black(0, 0, 0);

// zone color for this dropper
#define ZONE_COLOR (red) // (yellow), (green), (blue)

Servo servo;
NeoPixelBus<NeoBrgFeature, NeoWs2811Method> strip(NUM_PIXELS);

unsigned long last_debounce = 0;
int last_debounce_state = HIGH;
int button_state = HIGH;
unsigned long start_time = 0;
short gate_state = OPEN;
char msg[50];
char topic[50];

// this defines which order droppers activate
short drop_order = 0;

enum {
    running = 0,
    stopped
} led_state = stopped;

enum {
    not_connected,
    find_mdns,
    mqtt_connected,
    mqtt_established
} wifi_state = not_connected;

WiFiClient espClient;
PubSubClient client(espClient);
WiFiUDP udp;
Resolver resolver(udp);
IPAddress ip = INADDR_NONE;

// servo routines

void drop_cube() {
    servo.write(180);
    gate_state = OPEN;
}

void load_cube() {
    servo.write(0);
    gate_state = CLOSED;
}

void toggle_gate() {
    if (gate_state == OPEN) {
        servo.write(0);
        gate_state = CLOSED;
    } else {
        servo.write(180);
        gate_state = OPEN;
    }
}

// LED routines

void set_led_black() {
    for (int i = 0; i < NUM_PIXELS; i++)
        strip.SetPixelColor(i, black);
}

void set_led_zones() {
    for (int i = 0; i < NUM_PIXELS; i++)
        strip.SetPixelColor(i, ZONE_COLOR);
}

/* old animations

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

*/


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (unsigned int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    if (strncmp((char *)payload, "run", length) == 0) {
        led_state = running;
        start_time = millis();
    }

    if (strncmp((char *)payload, "zones", length) == 0) {
        led_state = stopped;
        set_led_zones();
    }

    if (strncmp((char *)payload, "stop", length) == 0 ) {
        led_state = stopped;
        set_led_black();
    }

    if (*((char*)payload) == 'o' && length > 1) {
        drop_order = *((char *)(payload + 1)) - '0';
    }

    // update the LED strip
    strip.Show();
}

void setup() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(HOSTNAME);

    Serial.begin(115200);

    // init wifi
    Serial.println();
    Serial.print("wifi ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // init LED strip(s)
    strip.Begin();
    strip.Show();
    set_led_black();
    strip.SetPixelColor(0, red);
    strip.Show();
    start_time = millis();

    servo.attach(SERVO_PIN);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // open servo initially for cube load
    servo.write(180);
    gate_state = OPEN;
    delay(500);
}

void loop() {

    // Deal with wifi and MQTT if connected

    // Connectity State Machine
    switch (wifi_state) {
        case not_connected:
            strip.SetPixelColor(0, red);
            strip.Show();
            if (WiFi.status() == WL_CONNECTED) {
                randomSeed(micros());
                ip = WiFi.localIP();
                Serial.println("ip: ");
                Serial.println(ip);
                wifi_state = find_mdns;
            }
            break;
        case find_mdns:
        {
            strip.SetPixelColor(0, blue);
            strip.Show();
            IPAddress RC_A_ip = resolver.search(ARENA_NAME);
            if (RC_A_ip != INADDR_NONE) {
                // init MQTT
                Serial.print("arena: ");
                Serial.println(RC_A_ip);
                client.setServer(RC_A_ip, 1883);
                client.setCallback(mqtt_callback);
                wifi_state = mqtt_connected;
            } else
                Serial.println("Cannot find mdns for Arena");
        }
        case mqtt_connected:
            // connect to mqtt
            strip.SetPixelColor(0, yellow);
            strip.Show();
            if (client.connected()) {
                Serial.print("mqtt established");
                wifi_state = mqtt_established;
                strip.SetPixelColor(0, green);
                strip.Show();
            } else {
                // Attempt to connect
                if (client.connect(HOSTNAME)) {
                    // Once connected, publish an announcement...
                    snprintf(topic, 50, "tele/%s/STATE", HOSTNAME);
                    snprintf(msg, 50, "tele/%s/STATE {\"ip\":\"%d.%d.%d.%d\"}", HOSTNAME, ip[0], ip[1], ip[2], ip[3]);
                    client.publish(topic, msg);
                    snprintf(topic, 50, "cmnd/%s", HOSTNAME);
                    client.subscribe(topic);
                }
            }
            break;
        case mqtt_established:
            if (client.connected()) {
                // run mqtt tasks
                client.loop();
            } else {
                Serial.print("mqtt failed");
                if (WiFi.status() == WL_CONNECTED)
                    wifi_state = find_mdns;
                else
                    wifi_state = not_connected;
            }
            break;
    }

    // LED state machine

    int round_time = millis() - start_time;

    switch (led_state) {
        case running:
            // <insert idle anim here>

            // 60, 80, 100, ...
            if ((round_time / 1000) >= ((drop_order * 20 + 60)) && gate_state == CLOSED) {
                Serial.println("dropping cube");
                drop_cube();
            }
            break;
        default:
            break;
    }

    for (int i = 0; i < NUM_PIXELS; i++)
        strip.SetPixelColor(i, red);
    
    strip.Show();

    Serial.println("loop");

    // handle button presses

    button_state = digitalRead(BUTTON_PIN);
    // button pressed is LOW, button not pressed is HIGH
    if (button_state == LOW && last_debounce_state == HIGH) {
        last_debounce = millis();
        last_debounce_state = LOW;
        Serial.println("button pressed");
        toggle_gate();
    }
    if ((millis() - last_debounce) > DEBOUNCE_TIME && button_state == HIGH)
        last_debounce_state = HIGH;
}
