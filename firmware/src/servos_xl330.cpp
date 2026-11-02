#include "servos_xl330.h"

namespace {
constexpr uint16_t ADDR_OPERATING_MODE = 11;
constexpr uint16_t ADDR_TORQUE_ENABLE = 64;
constexpr uint16_t ADDR_BUS_WATCHDOG = 98;
constexpr uint16_t ADDR_GOAL_CURRENT = 102;
constexpr uint16_t ADDR_PRESENT_CURRENT = 126;
constexpr uint16_t ADDR_PRESENT_POSITION = 132;
constexpr uint16_t ADDR_PRESENT_TEMPERATURE = 146;
constexpr uint8_t READ_LEN = 10;
constexpr uint8_t MODE_CURRENT = 0;
constexpr uint8_t WATCHDOG_TRIPPED = 0xFF;
constexpr uint8_t STATUS_ALERT = 0x80;
constexpr float TICKS_PER_DEG = 4096.0f / 360.0f;
constexpr int BEGIN_RETRIES = 3;

int32_t le32(const uint8_t *d) {
    return int32_t(uint32_t(d[0]) | (uint32_t(d[1]) << 8) | (uint32_t(d[2]) << 16) | (uint32_t(d[3]) << 24));
}
}

bool XL330Servos::begin() {
    bus_.begin(Serial1, SERVO_RX_PIN, SERVO_TX_PIN, SERVO_BAUD, SERVO_REPLY_TIMEOUT_US);

    for (int i = 0; i < NUM_CHANNELS; i++) {
        bool alive = false;
        for (int r = 0; r < BEGIN_RETRIES && !alive; r++) alive = bus_.ping(SERVO_IDS[i]);
        if (!alive) return false;
    }
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!init_servo(i)) return false;
    }

    uint8_t buf[NUM_CHANNELS * READ_LEN];
    bool got[NUM_CHANNELS];
    uint8_t errors[NUM_CHANNELS];
    bool all = false;
    for (int r = 0; r < BEGIN_RETRIES && !all; r++) {
        all = bus_.sync_read(ADDR_PRESENT_CURRENT, READ_LEN, SERVO_IDS, NUM_CHANNELS, buf, got, errors) == NUM_CHANNELS;
    }
    if (!all) return false;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        position_[i] = le32(buf + i * READ_LEN + (ADDR_PRESENT_POSITION - ADDR_PRESENT_CURRENT));
        open_ticks_[i] = SERVO_OPEN_AT_BOOT ? position_[i] : int32_t(SERVO_OPEN_DEG[i] * TICKS_PER_DEG);
        missed_[i] = 0;
        lost_[i] = false;
        answering_[i] = true;
        alert_[i] = false;
    }
    last_slow_ms_ = last_recover_ms_ = millis();
    return true;
}

bool XL330Servos::init_servo(int i) {
    uint8_t id = SERVO_IDS[i];
    bool ok = bus_.write(id, ADDR_TORQUE_ENABLE, 0, 1);
    ok &= bus_.write(id, ADDR_OPERATING_MODE, MODE_CURRENT, 1);
    ok &= bus_.write(id, ADDR_BUS_WATCHDOG, 0, 1);
    ok &= bus_.write(id, ADDR_GOAL_CURRENT, 0, 2);
    ok &= bus_.write(id, ADDR_TORQUE_ENABLE, 1, 1);
    ok &= bus_.write(id, ADDR_BUS_WATCHDOG, SERVO_WATCHDOG_X20MS, 1);
    return ok;
}

void XL330Servos::read(float closure[NUM_CHANNELS], float torque[NUM_CHANNELS]) {
    uint8_t buf[NUM_CHANNELS * READ_LEN];
    bool got[NUM_CHANNELS];
    uint8_t errors[NUM_CHANNELS];
    bus_.sync_read(ADDR_PRESENT_CURRENT, READ_LEN, SERVO_IDS, NUM_CHANNELS, buf, got, errors);

    for (int i = 0; i < NUM_CHANNELS; i++) {
        answering_[i] = got[i];
        if (got[i]) {
            const uint8_t *d = buf + i * READ_LEN;
            current_ma_[i] = int16_t(d[0] | (d[1] << 8));
            position_[i] = le32(d + (ADDR_PRESENT_POSITION - ADDR_PRESENT_CURRENT));
            missed_[i] = 0;
            if (errors[i] & STATUS_ALERT) alert_[i] = true;
        } else if (missed_[i] < SERVO_LOST_CYCLES) {
            missed_[i]++;
        } else {
            lost_[i] = true;
        }
        float range_ticks = SERVO_RANGE_DEG[i] * TICKS_PER_DEG;
        closure_[i] = SERVO_CLOSE_DIR[i] * (position_[i] - open_ticks_[i]) / range_ticks;
        closure[i] = closure_[i];
        torque[i] = lost_[i] ? 0.0f : -SERVO_CLOSE_DIR[i] * current_ma_[i] * 1e-3f * SERVO_TORQUE_NM_PER_A;
    }
}

void XL330Servos::write(const float torque[NUM_CHANNELS], bool enable) {
    bool on = enable && !fault();
    uint32_t goal[NUM_CHANNELS];
    for (int i = 0; i < NUM_CHANNELS; i++) {
        float fade_in = constrain(closure_[i] / CLOSURE_FADE_IN, 0.0f, 1.0f);
        float t = (on && isfinite(torque[i])) ? torque[i] * fade_in * temperature_scale(i) : 0.0f;
        float ma = -SERVO_CLOSE_DIR[i] * t / SERVO_TORQUE_NM_PER_A * 1e3f;
        int16_t clamped = int16_t(constrain(lroundf(ma), -long(SERVO_CURRENT_LIMIT_MA), long(SERVO_CURRENT_LIMIT_MA)));
        goal[i] = uint16_t(clamped);
    }
    bus_.sync_write(ADDR_GOAL_CURRENT, 2, SERVO_IDS, goal, NUM_CHANNELS);

    if (fault()) {
        recover();
    } else if (millis() - last_slow_ms_ >= SLOW_POLL_PERIOD_MS) {
        poll_slow();
    }
}

bool XL330Servos::fault() const {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (lost_[i] || alert_[i]) return true;
    }
    return false;
}

void XL330Servos::recover() {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (lost_[i] && answering_[i]) {
            if (init_servo(i)) {
                lost_[i] = false;
                alert_[i] = false;
            }
            return;
        }
    }
    if (millis() - last_recover_ms_ < SERVO_RETRY_MS) return;
    last_recover_ms_ = millis();
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (alert_[i] && !lost_[i]) {
            bus_.reboot(SERVO_IDS[i]);
            lost_[i] = true;
            missed_[i] = SERVO_LOST_CYCLES;
            return;
        }
    }
}

void XL330Servos::poll_slow() {
    last_slow_ms_ = millis();
    int i = slow_index_;
    slow_index_ = (slow_index_ + 1) % NUM_CHANNELS;
    uint8_t id = SERVO_IDS[i];

    uint32_t value;
    if (bus_.read(id, ADDR_PRESENT_TEMPERATURE, 1, value)) temperature_c_[i] = value;
    if (bus_.read(id, ADDR_BUS_WATCHDOG, 1, value) && value == WATCHDOG_TRIPPED) {
        bus_.write(id, ADDR_BUS_WATCHDOG, 0, 1);
        bus_.write(id, ADDR_BUS_WATCHDOG, SERVO_WATCHDOG_X20MS, 1);
    }
}

float XL330Servos::temperature_scale(int i) const {
    float t = temperature_c_[i];
    if (t <= TEMP_FADE_START_C) return 1.0f;
    if (t >= TEMP_FADE_END_C) return 0.0f;
    return (TEMP_FADE_END_C - t) / (TEMP_FADE_END_C - TEMP_FADE_START_C);
}
