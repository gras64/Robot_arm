#ifndef MICRO_ROS_CONFIG_H
#define MICRO_ROS_CONFIG_H

// Micro-ROS Konfiguration
#define MICRO_ROS_TRANSPORT_SERIAL

// CAN Bus Konfiguration
#define CAN_BUS_SPEED 500000UL
#define CAN_BUS_CS_PIN 10
#define CAN_BUS_INT_PIN 2

// Motor Konfiguration
#define MOTOR_COUNT 6
#define MOTOR_PIN_1 25
#define MOTOR_PIN_2 26
#define MOTOR_PIN_3 27
#define MOTOR_PIN_4 14
#define MOTOR_PIN_5 12
#define MOTOR_PIN_6 13

// CAN IDs für jeden Motor
#define MOTOR_1_CAN_ID 0x100
#define MOTOR_2_CAN_ID 0x101
#define MOTOR_3_CAN_ID 0x102
#define MOTOR_4_CAN_ID 0x103
#define MOTOR_5_CAN_ID 0x104
#define MOTOR_6_CAN_ID 0x105

// Motorsteuerung Konfiguration
#define MOTOR_MAX_POSITION 1000
#define MOTOR_MIN_POSITION -1000
#define MOTOR_MAX_SPEED 1000
#define MOTOR_MIN_SPEED -1000

// Per-motor limits (override defaults above per motor if defined)
#define MOTOR_1_MIN_POSITION -1000
#define MOTOR_1_MAX_POSITION 1000
#define MOTOR_1_MAX_SPEED 1000

#define MOTOR_2_MIN_POSITION -1000
#define MOTOR_2_MAX_POSITION 1000
#define MOTOR_2_MAX_SPEED 1000

#define MOTOR_3_MIN_POSITION -1000
#define MOTOR_3_MAX_POSITION 1000
#define MOTOR_3_MAX_SPEED 1000

#define MOTOR_4_MIN_POSITION -1000
#define MOTOR_4_MAX_POSITION 1000
#define MOTOR_4_MAX_SPEED 1000

#define MOTOR_5_MIN_POSITION -1000
#define MOTOR_5_MAX_POSITION 1000
#define MOTOR_5_MAX_SPEED 1000

#define MOTOR_6_MIN_POSITION -1000
#define MOTOR_6_MAX_POSITION 1000
#define MOTOR_6_MAX_SPEED 1000

// MKS SERVO CAN parameter command IDs (adjust to match manual/MKSServoCAN)
// These IDs are used by `sendCANMotorConfig()` to send parameter write frames.
#define MKS_PARAM_CMD_WRITE 0x80
#define MKS_PARAM_ID_MIN_POS 0x01
#define MKS_PARAM_ID_MAX_POS 0x02
#define MKS_PARAM_ID_MAX_SPEED 0x03

// Micro-ROS Node Konfiguration
#define ROS_NODE_NAME "robot_arm_node"
#define ROS_NODE_NAMESPACE "robot_arm"

// Threading Konfiguration
#define ROS_THREAD_STACK_SIZE 8192
#define ROS_THREAD_PRIORITY 1

#endif // MICRO_ROS_CONFIG_H