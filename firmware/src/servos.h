#pragma once

#include "config.h"

class Servos {
public:
    virtual ~Servos() = default;

    /** Bring up the servos; false if they are not reachable. */
    virtual bool begin() = 0;

    /** Read closure (0 open, 1 closed) and measured torque in Nm per channel. */
    virtual void read(float closure[NUM_CHANNELS], float torque[NUM_CHANNELS]) = 0;

    /** Command a resisting torque in Nm per channel; enable false releases all servos. */
    virtual void write(const float torque[NUM_CHANNELS], bool enable) = 0;

    /** True for the stand-in without hardware. */
    virtual bool is_stub() const { return false; }
};
