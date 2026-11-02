#pragma once

#include <Arduino.h>

constexpr int NUM_CHANNELS = 3;

constexpr int LED_GREEN_PIN = 4;
constexpr int LED_RED_PIN = 6;
constexpr int KILL_PIN = 5;
constexpr bool KILL_SWITCH_NC = false;  // false: normally open, pressed pulls the pin to GND | true: normally closed, pressed or broken wire opens it
constexpr uint32_t LED_SLOW_BLINK_MS = 1000;
constexpr uint32_t LED_FAST_BLINK_MS = 200;
constexpr uint32_t LED_HEARTBEAT_MS = 2000;
constexpr uint32_t LED_HEARTBEAT_ON_MS = 100;

constexpr uint32_t UART_BAUD = 921600;
constexpr uint32_t STATE_PERIOD_US = 2000;
constexpr uint32_t FEEDBACK_TIMEOUT_US = 50000;

constexpr float TORQUE_LIMIT = 0.2f;
constexpr float DAMPING_LIMIT = 0.05f;
constexpr float VELOCITY_FILTER_HZ = 30.0f;
constexpr float ENABLE_RAMP_S = 0.8f;

constexpr bool USE_STUB = false;  // false: XL330 servos, stub if they do not answer | true: always stub

constexpr int SERVO_TX_PIN = 17;
constexpr int SERVO_RX_PIN = 18;
constexpr uint32_t SERVO_BAUD = 1000000;
constexpr uint32_t SERVO_REPLY_TIMEOUT_US = 1000;
constexpr uint32_t SERVO_RETRY_MS = 1000;
constexpr uint16_t SERVO_LOST_CYCLES = 25;
constexpr uint8_t SERVO_IDS[NUM_CHANNELS] = {1, 2, 3};  // thumb, index, middle
constexpr int8_t SERVO_CLOSE_DIR[NUM_CHANNELS] = {1, 1, 1};  // +1 | -1: position direction that closes the finger
constexpr bool SERVO_OPEN_AT_BOOT = true;  // true: position at boot is open | false: SERVO_OPEN_DEG is open
constexpr float SERVO_OPEN_DEG[NUM_CHANNELS] = {0.0f, 0.0f, 0.0f};
constexpr float SERVO_RANGE_DEG[NUM_CHANNELS] = {90.0f, 90.0f, 90.0f};
constexpr float SERVO_TORQUE_NM_PER_A = 0.15f;
constexpr int16_t SERVO_CURRENT_LIMIT_MA = 500;
constexpr uint8_t SERVO_WATCHDOG_X20MS = 5;
constexpr float CLOSURE_FADE_IN = 0.05f;
constexpr float TEMP_FADE_START_C = 55.0f;
constexpr float TEMP_FADE_END_C = 65.0f;
constexpr uint32_t SLOW_POLL_PERIOD_MS = 300;

constexpr float STUB_PERIOD_S = 3.0f;
constexpr float STUB_STIFFNESS = 0.5f;
constexpr float STUB_NATURAL_HZ = 5.0f;
constexpr float STUB_DAMPING_RATIO = 0.7f;
