#pragma once

#include <Arduino.h>

constexpr int NUM_CHANNELS = 3;

constexpr int LED_PIN = 4;
constexpr int KILL_PIN = 5;

constexpr uint32_t UART_BAUD = 921600;
constexpr uint32_t STATE_PERIOD_US = 2000;
constexpr uint32_t FEEDBACK_TIMEOUT_US = 50000;

constexpr float TORQUE_LIMIT = 0.2f;
constexpr float DAMPING_LIMIT = 0.05f;
constexpr float VELOCITY_FILTER_HZ = 30.0f;

constexpr float STUB_PERIOD_S = 3.0f;
constexpr float STUB_STIFFNESS = 0.5f;
constexpr float STUB_NATURAL_HZ = 5.0f;
constexpr float STUB_DAMPING_RATIO = 0.7f;
