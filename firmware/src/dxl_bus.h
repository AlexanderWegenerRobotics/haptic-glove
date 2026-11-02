#pragma once

#include <Arduino.h>

class DxlBus {
public:
    /** Start the half-duplex bus: TX through a resistor and RX on the same data line. */
    void begin(HardwareSerial &port, int rx_pin, int tx_pin, uint32_t baud, uint32_t reply_timeout_us);

    /** True if the servo answers a ping. */
    bool ping(uint8_t id);

    /** Write 1, 2 or 4 bytes to one register and check the status reply. */
    bool write(uint8_t id, uint16_t addr, uint32_t value, uint8_t size);

    /** Read 1, 2 or 4 bytes from one register. */
    bool read(uint8_t id, uint16_t addr, uint8_t size, uint32_t &value);

    /** Write one register of the same size on several servos in one packet, without replies. */
    void sync_write(uint16_t addr, uint8_t size, const uint8_t *ids, const uint32_t *values, uint8_t count);

    /** Read len bytes from several servos with one request into out (count x len) and their status error bytes; returns how many answered. */
    uint8_t sync_read(uint16_t addr, uint8_t len, const uint8_t *ids, uint8_t count, uint8_t *out, bool *ok, uint8_t *errors);

    /** Restart one servo, which clears a hardware error alert; it answers again after it has booted. */
    void reboot(uint8_t id);

private:
    struct Status {
        uint8_t id;
        uint8_t error;
        uint8_t params[32];
        uint16_t count;
    };

    /** Dynamixel protocol 2.0 CRC-16 (poly 0x8005, init 0). */
    static uint16_t crc(const uint8_t *data, size_t len);

    /** Next received byte from a local cache refilled with one bulk read; false if nothing is pending. */
    bool next_byte(uint8_t &b);

    /** Drop everything pending in the UART and the cache. */
    void clear_rx();

    /** Send one instruction packet and discard its own echo from the shared line. */
    void send(uint8_t id, uint8_t inst, const uint8_t *params, uint16_t count);

    /** Read one status packet; false on timeout, bad CRC or wrong instruction. */
    bool receive(Status &status);

    HardwareSerial *port_ = nullptr;
    uint32_t timeout_us_ = 1000;
    uint8_t cache_[128];
    size_t head_ = 0;
    size_t len_ = 0;
};
