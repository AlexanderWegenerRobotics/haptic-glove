#pragma once

#include "servos.h"

class StubServos : public Servos {
public:
    /** Start the simulated fingers open. */
    bool begin() override;

    /** Advance the simulated operator fingers and return closure and applied torque. */
    void read(float closure[NUM_CHANNELS], float torque[NUM_CHANNELS]) override;

    /** Remember the commanded torque. */
    void write(const float torque[NUM_CHANNELS], bool enable) override;

    /** Always a stub. */
    bool is_stub() const override { return true; }

private:
    float torque_[NUM_CHANNELS] = {};
    float position_[NUM_CHANNELS] = {};
    float velocity_[NUM_CHANNELS] = {};
    uint32_t last_us_ = 0;
};
