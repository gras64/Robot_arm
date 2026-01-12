#include "motor_control.h"

MotorControl::MotorControl(uint8_t pin) : motor_pin(pin), current_angle(0), current_speed(0), enabled(false) {
    pinMode(motor_pin, OUTPUT);
    digitalWrite(motor_pin, LOW);
}

void MotorControl::setAngle(int16_t angle) {
    // Begrenzung der Winkelwerte
    if (angle > 1000) angle = 1000;
    if (angle < -1000) angle = -1000;
    
    current_angle = angle;
    
    // Konvertierung in PWM-Wert (0-255)
    int pwm_value = map(angle, -1000, 1000, 0, 255);
    analogWrite(motor_pin, pwm_value);
}

void MotorControl::setSpeed(int16_t speed) {
    // Begrenzung der Geschwindigkeitswerte
    if (speed > 1000) speed = 1000;
    if (speed < -1000) speed = -1000;
    
    current_speed = speed;
}

void MotorControl::enable() {
    enabled = true;
    // Hier könnte eine spezielle Aktivierung für den MKS SERVO42D erfolgen
}

void MotorControl::disable() {
    enabled = false;
    digitalWrite(motor_pin, LOW);
}

int16_t MotorControl::getAngle() {
    return current_angle;
}

int16_t MotorControl::getSpeed() {
    return current_speed;
}