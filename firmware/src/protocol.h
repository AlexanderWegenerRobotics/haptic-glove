#pragma once

#include <Arduino.h>

#include "config.h"

constexpr uint8_t PROTOCOL_VERSION = 2;
constexpr uint8_t SYNC0 = 0xA5;
constexpr uint8_t SYNC1 = 0x5A;
constexpr uint8_t TYPE_STATE = 0x01;
constexpr uint8_t TYPE_FEEDBACK = 0x02;
constexpr uint8_t MAX_PAYLOAD = 64;
constexpr size_t MAX_FRAME = MAX_PAYLOAD + 6;

constexpr uint8_t STATE_KILL = 1;
constexpr uint8_t STATE_TORQUE_ON = 2;
constexpr uint8_t STATE_TIMEOUT = 4;
constexpr uint8_t STATE_STUB = 8;
constexpr uint8_t FEEDBACK_ENABLE = 1;

#pragma pack(push, 1)
struct StatePayload {
    uint8_t version;
    uint32_t seq;
    uint32_t t_us;
    float closure[NUM_CHANNELS];
    float torque[NUM_CHANNELS];
    uint32_t rtt_us;
    uint8_t flags;
};

struct FeedbackPayload {
    uint32_t seq;
    uint32_t echo_seq;
    uint32_t echo_t_us;
    float torque[NUM_CHANNELS];
    float damping;
    float slew;
    uint8_t flags;
};
#pragma pack(pop)

static_assert(sizeof(StatePayload) == 38, "state payload size");
static_assert(sizeof(FeedbackPayload) == 33, "feedback payload size");

/** CRC-16/CCITT-FALSE, chainable through crc. */
uint16_t crc16(const uint8_t *data, size_t len, uint16_t crc = 0xFFFF);

/** Write one frame into out (at least MAX_FRAME bytes) and return its length. */
size_t encode_frame(uint8_t type, const void *payload, uint8_t len, uint8_t *out);

class FrameParser {
public:
    /** Feed one byte; true when a complete frame with a valid CRC is ready. */
    bool feed(uint8_t byte);

    /** Type of the last complete frame. */
    uint8_t type() const { return type_; }

    /** Payload of the last complete frame. */
    const uint8_t *payload() const { return buf_; }

    /** Payload length of the last complete frame. */
    uint8_t length() const { return len_; }

    uint32_t crc_errors = 0;

private:
    enum State { WAIT_SYNC0, WAIT_SYNC1, READ_TYPE, READ_LEN, READ_PAYLOAD, READ_CRC_LO, READ_CRC_HI };
    State state_ = WAIT_SYNC0;
    uint8_t type_ = 0;
    uint8_t len_ = 0;
    uint8_t idx_ = 0;
    uint16_t crc_rx_ = 0;
    uint8_t buf_[MAX_PAYLOAD];
};
