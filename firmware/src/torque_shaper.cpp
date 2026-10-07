#include "torque_shaper.h"

void TorqueShaper::update(const float closure[NUM_CHANNELS], const float target[NUM_CHANNELS], float damping,
                          float slew, bool enable, float dt, float out[NUM_CHANNELS]) {
    float alpha = expf(-TWO_PI * VELOCITY_FILTER_HZ * dt);
    float b = isfinite(damping) ? constrain(damping, 0.0f, DAMPING_LIMIT) : 0.0f;
    float step = (isfinite(slew) && slew > 0.0f) ? slew * dt : TORQUE_LIMIT;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (primed_) velocity_[i] = alpha * velocity_[i] + (1.0f - alpha) * (closure[i] - previous_[i]) / dt;
        previous_[i] = closure[i];
        if (!enable) {
            output_[i] = 0.0f;
        } else {
            float base = isfinite(target[i]) ? constrain(target[i], 0.0f, TORQUE_LIMIT) : 0.0f;
            float desired = base > 0.0f ? constrain(base + b * max(velocity_[i], 0.0f), 0.0f, TORQUE_LIMIT) : 0.0f;
            output_[i] += constrain(desired - output_[i], -step, step);
        }
        out[i] = output_[i];
    }
    primed_ = true;
}
