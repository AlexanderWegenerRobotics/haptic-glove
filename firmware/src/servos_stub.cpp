#include "servos_stub.h"

bool StubServos::begin() {
    last_us_ = micros();
    return true;
}

void StubServos::read(float closure[NUM_CHANNELS], float torque[NUM_CHANNELS]) {
    uint32_t now = micros();
    float dt = min((now - last_us_) * 1e-6f, 0.01f);
    last_us_ = now;
    float t = now * 1e-6f;
    float omega = TWO_PI * STUB_NATURAL_HZ;
    float inertia = STUB_STIFFNESS / (omega * omega);
    float damping = 2.0f * STUB_DAMPING_RATIO * sqrtf(STUB_STIFFNESS * inertia);
    for (int i = 0; i < NUM_CHANNELS; i++) {
        float desired = 0.5f * (1.0f - cosf(TWO_PI * t / STUB_PERIOD_S - 0.3f * i));
        float accel = (STUB_STIFFNESS * (desired - position_[i]) - damping * velocity_[i] - torque_[i]) / inertia;
        velocity_[i] += accel * dt;
        position_[i] += velocity_[i] * dt;
        if (position_[i] < 0.0f || position_[i] > 1.0f) {
            position_[i] = constrain(position_[i], 0.0f, 1.0f);
            velocity_[i] = 0.0f;
        }
        closure[i] = position_[i];
        torque[i] = torque_[i];
    }
}

void StubServos::write(const float torque[NUM_CHANNELS], bool enable) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
        torque_[i] = enable ? torque[i] : 0.0f;
    }
}
