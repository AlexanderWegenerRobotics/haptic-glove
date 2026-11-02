#pragma once

#include "config.h"

class TorqueShaper {
public:
    /** Turn the host torque into the servo torque: contact damping on closing velocity, slew limit, clamp, soft start after enabling; zero when disabled. */
    void update(const float closure[NUM_CHANNELS], const float target[NUM_CHANNELS], float damping, float slew,
                bool enable, float dt, float out[NUM_CHANNELS]);

private:
    float previous_[NUM_CHANNELS] = {};
    float velocity_[NUM_CHANNELS] = {};
    float output_[NUM_CHANNELS] = {};
    float ramp_ = 0.0f;
    bool primed_ = false;
};
