#include <Arduino.h>

#include "config.h"
#include "protocol.h"
#include "servos_stub.h"
#include "torque_shaper.h"

StubServos stub_servos;
Servos &servos = stub_servos;
TorqueShaper shaper;

FrameParser usb_parser;
FrameParser uart_parser;
FeedbackPayload feedback{};

uint32_t state_seq = 0;
uint32_t next_state_us = 0;
uint32_t last_feedback_us = 0;
uint32_t rtt_us = 0;
bool have_feedback = false;

/** Take over a valid feedback frame and measure the round trip. */
void handle_frame(const FrameParser &parser) {
    if (parser.type() != TYPE_FEEDBACK || parser.length() != sizeof(FeedbackPayload)) return;
    memcpy(&feedback, parser.payload(), sizeof(FeedbackPayload));
    uint32_t now = micros();
    last_feedback_us = now;
    rtt_us = now - feedback.echo_t_us;
    have_feedback = true;
}

/** Drain one port into its parser. */
void poll(Stream &port, FrameParser &parser) {
    while (port.available() > 0) {
        if (parser.feed(port.read())) handle_frame(parser);
    }
}

/** Write a frame only if it fits into the TX buffer, so a port nobody reads never blocks the loop. */
void send(Stream &port, const uint8_t *frame, size_t len) {
    if (port.availableForWrite() >= int(len)) port.write(frame, len);
}

/** LED on while the host link is alive, fast blink while the kill switch is pressed, off without a host. */
void update_led(uint32_t now, bool kill, bool timeout) {
    bool on = kill ? ((now / 100000) & 1) : !timeout;
    digitalWrite(LED_PIN, on ? HIGH : LOW);
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(KILL_PIN, INPUT_PULLUP);
    Serial.setRxBufferSize(1024);
    Serial.setTxTimeoutMs(0);
    Serial.begin(UART_BAUD);
    Serial0.setRxBufferSize(1024);
    Serial0.begin(UART_BAUD);
    servos.begin();
    next_state_us = micros();
}

void loop() {
    poll(Serial, usb_parser);
    poll(Serial0, uart_parser);

    uint32_t now = micros();
    if (int32_t(now - next_state_us) < 0) return;
    next_state_us += STATE_PERIOD_US;
    if (int32_t(now - next_state_us) > int32_t(STATE_PERIOD_US)) next_state_us = now + STATE_PERIOD_US;

    bool kill = digitalRead(KILL_PIN) == LOW;
    bool timeout = !have_feedback || now - last_feedback_us > FEEDBACK_TIMEOUT_US;
    bool enable = !kill && !timeout && (feedback.flags & FEEDBACK_ENABLE);

    StatePayload state{};
    state.version = PROTOCOL_VERSION;
    float measured[NUM_CHANNELS];
    servos.read(state.closure, measured);

    float torque_cmd[NUM_CHANNELS];
    shaper.update(state.closure, feedback.torque, feedback.damping, feedback.slew, enable, STATE_PERIOD_US * 1e-6f,
                  torque_cmd);
    servos.write(torque_cmd, enable);

    memcpy(state.torque, measured, sizeof(measured));
    state.seq = ++state_seq;
    state.t_us = micros();
    state.rtt_us = rtt_us;
    state.flags = (kill ? STATE_KILL : 0) | (enable ? STATE_TORQUE_ON : 0) | (timeout ? STATE_TIMEOUT : 0) |
                  (servos.is_stub() ? STATE_STUB : 0);

    uint8_t frame[MAX_FRAME];
    size_t len = encode_frame(TYPE_STATE, &state, sizeof(state), frame);
    send(Serial, frame, len);
    send(Serial0, frame, len);

    update_led(now, kill, timeout);
}
