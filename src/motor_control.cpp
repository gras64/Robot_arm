#include "motor_control.h"

MotorControl::MotorControl(uint8_t pin) : motor_pin(pin), current_angle(0), current_speed(0), enabled(false), report_changes(false), last_sensor_angle(0), sensor_threshold(2), use_pwm(true) {
    pinMode(motor_pin, OUTPUT);
    digitalWrite(motor_pin, LOW);
}

void MotorControl::setAngle(int16_t angle) {
    // Begrenzung der Winkelwerte
    if (angle > 1000) angle = 1000;
    if (angle < -1000) angle = -1000;
    
    current_angle = angle;
    
    // Konvertierung in PWM-Wert (0-255) und Ausgabe nur, wenn PWM aktiviert ist
    int pwm_value = map(angle, -1000, 1000, 0, 255);
    if (use_pwm) {
        analogWrite(motor_pin, pwm_value);
    }
    if (report_changes) {
        Serial.print("Motor ");
        Serial.print(motor_pin);
        Serial.print(" setAngle ");
        Serial.print(current_angle);
        if (!use_pwm) Serial.print(" (PWM disabled)");
        Serial.println();
    }
}

void MotorControl::setSpeed(int16_t speed) {
    // Begrenzung der Geschwindigkeitswerte
    if (speed > 1000) speed = 1000;
    if (speed < -1000) speed = -1000;
    
    current_speed = speed;
    if (report_changes) {
        Serial.print("Motor ");
        Serial.print(motor_pin);
        Serial.print(" setSpeed ");
        Serial.print(current_speed);
        if (!use_pwm) Serial.print(" (PWM disabled)");
        Serial.println();
    }
}

void MotorControl::enable() {
    enabled = true;
    // Hier könnte eine spezielle Aktivierung für den MKS SERVO42D erfolgen
    if (report_changes) {
        Serial.print("Motor ");
        Serial.print(motor_pin);
        Serial.println(" enabled");
    }
}

void MotorControl::disable() {
    enabled = false;
    digitalWrite(motor_pin, LOW);
    if (report_changes) {
        Serial.print("Motor ");
        Serial.print(motor_pin);
        Serial.println(" disabled");
    }
}

int16_t MotorControl::getAngle() {
    return current_angle;
}

int16_t MotorControl::getSpeed() {
    return current_speed;
}

void MotorControl::setReport(bool enable) {
    report_changes = enable;
}

bool MotorControl::isReporting() {
    return report_changes;
}

void MotorControl::setUsePWM(bool enable) {
    use_pwm = enable;
}

bool MotorControl::isUsingPWM() {
    return use_pwm;
}

void MotorControl::setSensorThreshold(int16_t threshold) {
    sensor_threshold = threshold;
}

void MotorControl::updateFromSensor(int16_t measured_angle) {
    // Detect movement beyond threshold
    int16_t delta = measured_angle - last_sensor_angle;
    if (delta < 0) delta = -delta;
    if (delta >= sensor_threshold) {
        last_sensor_angle = measured_angle;
        if (report_changes) {
            Serial.print("Motor ");
            Serial.print(motor_pin);
            Serial.print(" moved to sensorAngle ");
            Serial.print(measured_angle);
            Serial.print(" (delta ");
            Serial.print(delta);
            Serial.println(")");
        }
    }
}

int16_t MotorControl::getLastSensorAngle() {
    return last_sensor_angle;
}