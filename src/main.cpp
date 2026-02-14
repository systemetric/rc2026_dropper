#include <Arduino.h>
#include <Servo.h>

Servo servos[4];
int next_drop = 0;

void setup_servos() {
    servos[0].attach(D0);
    servos[0].write(0);

    servos[1].attach(D1);
    servos[1].write(0);

    servos[2].attach(D2);
    servos[2].write(0);

    servos[3].attach(D3);
    servos[3].write(0);

    delay(1000);
}

void activate_servo(Servo *servo) {
    servo->write(180);
    delay(1000);
    servo->write(0);
    delay(500);
}

void drop_cube() {
    if (next_drop >= 4)
        return;

    activate_servo(&servos[next_drop]);
    next_drop++;
}

void reset() {
    next_drop = 0;
}

void setup() {
    setup_servos();
    reset();
}

void loop() {
    drop_cube();
    delay(3000);
}
