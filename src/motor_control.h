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
    void setReport(bool enable);
    bool isReporting();
    void setUsePWM(bool enable);
    bool isUsingPWM();
    void setSensorThreshold(int16_t threshold);
    void updateFromSensor(int16_t measured_angle);
    int16_t getLastSensorAngle();
    
private:
    uint8_t motor_pin;
    int16_t current_angle;
    int16_t current_speed;
    bool enabled;
    bool report_changes;
    int16_t last_sensor_angle;
    int16_t sensor_threshold;
    bool use_pwm;
};

#endif // MOTOR_CONTROL_H