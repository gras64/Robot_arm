#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

// Platform / Arduino includes for integer types and pin functions
#include <Arduino.h>

// MKS SERVO42D Motorsteuerung
class MotorControl {
public:
    MotorControl(uint8_t pin);
    void setAngle(int16_t angle);
    void setSpeed(int16_t speed);
    void enable();
    void disable();
    int16_t getAngle();
    int16_t getSpeed();
    
private:
    uint8_t motor_pin;
    int16_t current_angle;
    int16_t current_speed;
    bool enabled;
};

#endif // MOTOR_CONTROL_H