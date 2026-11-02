#include "dxl_bus.h"

namespace {
constexpr uint8_t INST_PING = 0x01;
constexpr uint8_t INST_READ = 0x02;
constexpr uint8_t INST_WRITE = 0x03;
constexpr uint8_t INST_REBOOT = 0x08;
constexpr uint8_t INST_STATUS = 0x55;
constexpr uint8_t INST_SYNC_READ = 0x82;
constexpr uint8_t INST_SYNC_WRITE = 0x83;
constexpr uint8_t BROADCAST_ID = 0xFE;
constexpr size_t MAX_PACKET = 64;
}

void DxlBus::begin(HardwareSerial &port, int rx_pin, int tx_pin, uint32_t baud, uint32_t reply_timeout_us) {
    port_ = &port;
    timeout_us_ = reply_timeout_us;
    port_->begin(baud, SERIAL_8N1, rx_pin, tx_pin);
    clear_rx();
}

uint16_t DxlBus::crc(const uint8_t *data, size_t len) {
    uint16_t c = 0;
    for (size_t i = 0; i < len; i++) {
        c ^= uint16_t(data[i]) << 8;
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (c << 1) ^ 0x8005 : c << 1;
    }
    return c;
}

bool DxlBus::next_byte(uint8_t &b) {
    if (head_ < len_) {
        b = cache_[head_++];
        return true;
    }
    int avail = port_->available();
    if (avail <= 0) return false;
    len_ = port_->read(cache_, min(size_t(avail), sizeof(cache_)));
    head_ = 0;
    if (len_ == 0) return false;
    b = cache_[head_++];
    return true;
}

void DxlBus::clear_rx() {
    while (port_->available() > 0) port_->read(cache_, sizeof(cache_));
    head_ = len_ = 0;
}

void DxlBus::send(uint8_t id, uint8_t inst, const uint8_t *params, uint16_t count) {
    uint8_t pkt[MAX_PACKET];
    uint16_t length = count + 3;
    size_t n = 0;
    pkt[n++] = 0xFF;
    pkt[n++] = 0xFF;
    pkt[n++] = 0xFD;
    pkt[n++] = 0x00;
    pkt[n++] = id;
    pkt[n++] = length & 0xFF;
    pkt[n++] = length >> 8;
    pkt[n++] = inst;
    for (uint16_t i = 0; i < count && n < MAX_PACKET - 2; i++) pkt[n++] = params[i];
    uint16_t c = crc(pkt, n);
    pkt[n++] = c & 0xFF;
    pkt[n++] = c >> 8;

    clear_rx();
    port_->write(pkt, n);
    port_->flush();

    uint32_t start = micros();
    size_t echoed = 0;
    uint8_t b;
    while (echoed < n && micros() - start < timeout_us_) {
        if (next_byte(b)) echoed++;
    }
}

bool DxlBus::receive(Status &status) {
    static const uint8_t header[4] = {0xFF, 0xFF, 0xFD, 0x00};
    uint8_t buf[MAX_PACKET];
    size_t n = 0;
    size_t total = 0;
    uint32_t start = micros();

    while (micros() - start < timeout_us_) {
        uint8_t b;
        if (!next_byte(b)) continue;

        if (n < 4) {
            if (b == header[n]) {
                buf[n++] = b;
            } else if (n == 2 && b == 0xFF) {
                n = 2;
            } else if (b == 0xFF) {
                buf[0] = b;
                n = 1;
            } else {
                n = 0;
            }
            continue;
        }

        buf[n++] = b;
        if (n == 7) {
            total = 7 + (buf[5] | (buf[6] << 8));
            if (total > sizeof(buf) || total < 11) return false;
        }
        if (total && n == total) {
            uint16_t c = buf[total - 2] | (buf[total - 1] << 8);
            if (c != crc(buf, total - 2) || buf[7] != INST_STATUS) return false;
            status.id = buf[4];
            status.error = buf[8];
            status.count = total - 11;
            if (status.count > sizeof(status.params)) return false;
            memcpy(status.params, buf + 9, status.count);
            return true;
        }
    }
    return false;
}

bool DxlBus::ping(uint8_t id) {
    send(id, INST_PING, nullptr, 0);
    Status st;
    return receive(st) && st.id == id;
}

bool DxlBus::write(uint8_t id, uint16_t addr, uint32_t value, uint8_t size) {
    uint8_t p[6] = {uint8_t(addr & 0xFF), uint8_t(addr >> 8)};
    for (uint8_t i = 0; i < size; i++) p[2 + i] = (value >> (8 * i)) & 0xFF;
    send(id, INST_WRITE, p, 2 + size);
    Status st;
    return receive(st) && st.id == id && st.error == 0;
}

bool DxlBus::read(uint8_t id, uint16_t addr, uint8_t size, uint32_t &value) {
    uint8_t p[4] = {uint8_t(addr & 0xFF), uint8_t(addr >> 8), size, 0};
    send(id, INST_READ, p, 4);
    Status st;
    if (!receive(st) || st.id != id || st.error != 0 || st.count < size) return false;
    value = 0;
    for (uint8_t i = 0; i < size; i++) value |= uint32_t(st.params[i]) << (8 * i);
    return true;
}

void DxlBus::sync_write(uint16_t addr, uint8_t size, const uint8_t *ids, const uint32_t *values, uint8_t count) {
    uint8_t p[MAX_PACKET];
    size_t n = 0;
    p[n++] = addr & 0xFF;
    p[n++] = addr >> 8;
    p[n++] = size;
    p[n++] = 0;
    for (uint8_t k = 0; k < count && n + 1 + size <= MAX_PACKET - 12; k++) {
        p[n++] = ids[k];
        for (uint8_t i = 0; i < size; i++) p[n++] = (values[k] >> (8 * i)) & 0xFF;
    }
    send(BROADCAST_ID, INST_SYNC_WRITE, p, n);
}

uint8_t DxlBus::sync_read(uint16_t addr, uint8_t len, const uint8_t *ids, uint8_t count, uint8_t *out, bool *ok,
                          uint8_t *errors) {
    uint8_t p[MAX_PACKET];
    size_t n = 0;
    p[n++] = addr & 0xFF;
    p[n++] = addr >> 8;
    p[n++] = len;
    p[n++] = 0;
    for (uint8_t k = 0; k < count; k++) {
        p[n++] = ids[k];
        ok[k] = false;
        errors[k] = 0;
    }
    send(BROADCAST_ID, INST_SYNC_READ, p, n);

    uint8_t got = 0;
    for (uint8_t r = 0; r < count; r++) {
        Status st;
        if (!receive(st)) break;
        if (st.count < len) continue;
        for (uint8_t k = 0; k < count; k++) {
            if (ids[k] != st.id || ok[k]) continue;
            memcpy(out + k * len, st.params, len);
            ok[k] = true;
            errors[k] = st.error;
            got++;
        }
    }
    return got;
}

void DxlBus::reboot(uint8_t id) {
    send(id, INST_REBOOT, nullptr, 0);
    Status st;
    receive(st);
}
