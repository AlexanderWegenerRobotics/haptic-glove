#include "protocol.h"

uint16_t crc16(const uint8_t *data, size_t len, uint16_t crc) {
    for (size_t i = 0; i < len; i++) {
        crc ^= uint16_t(data[i]) << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

size_t encode_frame(uint8_t type, const void *payload, uint8_t len, uint8_t *out) {
    out[0] = SYNC0;
    out[1] = SYNC1;
    out[2] = type;
    out[3] = len;
    memcpy(out + 4, payload, len);
    uint16_t crc = crc16(out + 2, len + 2);
    out[4 + len] = crc & 0xFF;
    out[5 + len] = crc >> 8;
    return len + 6;
}

bool FrameParser::feed(uint8_t byte) {
    switch (state_) {
        case WAIT_SYNC0:
            if (byte == SYNC0) state_ = WAIT_SYNC1;
            return false;
        case WAIT_SYNC1:
            state_ = byte == SYNC1 ? READ_TYPE : (byte == SYNC0 ? WAIT_SYNC1 : WAIT_SYNC0);
            return false;
        case READ_TYPE:
            type_ = byte;
            state_ = READ_LEN;
            return false;
        case READ_LEN:
            if (byte > MAX_PAYLOAD) {
                state_ = WAIT_SYNC0;
                return false;
            }
            len_ = byte;
            idx_ = 0;
            state_ = len_ ? READ_PAYLOAD : READ_CRC_LO;
            return false;
        case READ_PAYLOAD:
            buf_[idx_++] = byte;
            if (idx_ == len_) state_ = READ_CRC_LO;
            return false;
        case READ_CRC_LO:
            crc_rx_ = byte;
            state_ = READ_CRC_HI;
            return false;
        case READ_CRC_HI: {
            crc_rx_ |= uint16_t(byte) << 8;
            state_ = WAIT_SYNC0;
            uint8_t head[2] = {type_, len_};
            if (crc16(buf_, len_, crc16(head, 2)) == crc_rx_) return true;
            crc_errors++;
            return false;
        }
    }
    return false;
}
