#pragma once

#include "dxl_bus.h"
#include "servos.h"

class XL330Servos : public Servos {
public:
    /** Ping all servos, switch them to current mode with zero current and torque on, set the open positions. */
    bool begin() override;

    /** Sync read position and current; closure 0 open, 1 closed, torque positive when resisting closing. */
    void read(float closure[NUM_CHANNELS], float torque[NUM_CHANNELS]) override;

    /** Convert resisting torque to goal currents (faded in over the first CLOSURE_FADE_IN of closure, none below open), sync write them, then handle recovery and slow polling. */
    void write(const float torque[NUM_CHANNELS], bool enable) override;

    /** True while a servo is lost or reports a hardware error. */
    bool fault() const override;

private:
    /** Torque off, current mode, zero goal current, torque on and bus watchdog for one channel. */
    bool init_servo(int i);

    /** Re-initialize a servo that answers again, or reboot one with a hardware error, at most one per call. */
    void recover();

    /** Read temperature and clear a tripped bus watchdog, one servo per call. */
    void poll_slow();

    /** Torque scale from 1 to 0 between the temperature fade limits. */
    float temperature_scale(int i) const;

    DxlBus bus_;
    int32_t open_ticks_[NUM_CHANNELS] = {};
    int32_t position_[NUM_CHANNELS] = {};
    int16_t current_ma_[NUM_CHANNELS] = {};
    float closure_[NUM_CHANNELS] = {};
    uint8_t temperature_c_[NUM_CHANNELS] = {};
    uint16_t missed_[NUM_CHANNELS] = {};
    bool lost_[NUM_CHANNELS] = {};
    bool answering_[NUM_CHANNELS] = {};
    bool alert_[NUM_CHANNELS] = {};
    uint32_t last_slow_ms_ = 0;
    uint32_t last_recover_ms_ = 0;
    int slow_index_ = 0;
};
