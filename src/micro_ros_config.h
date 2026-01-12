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

// Micro-ROS Node Konfiguration
#define ROS_NODE_NAME "robot_arm_node"
#define ROS_NODE_NAMESPACE "robot_arm"

// Threading Konfiguration
#define ROS_THREAD_STACK_SIZE 8192
#define ROS_THREAD_PRIORITY 1

#endif // MICRO_ROS_CONFIG_H