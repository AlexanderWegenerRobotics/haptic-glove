#include <Arduino.h>

#include "config.h"
#include "protocol.h"
#include "servos_stub.h"
#include "servos_xl330.h"
#include "torque_shaper.h"

XL330Servos xl330_servos;
StubServos stub_servos;
Servos *servos = &stub_servos;
TorqueShaper shaper;

FrameParser usb_parser;
FrameParser uart_parser;
FeedbackPayload feedback{};

uint32_t state_seq = 0;
uint32_t next_state_us = 0;
uint32_t last_feedback_us = 0;
uint32_t rtt_us = 0;
bool have_feedback = false;
uint32_t last_servo_try_ms = 0;

/** Take over a valid feedback frame and measure the round trip. */
void handle_frame(const FrameParser &parser) {
    if (parser.type() != TYPE_FEEDBACK || parser.length() != sizeof(FeedbackPayload)) return;
    memcpy(&feedback, parser.payload(), sizeof(FeedbackPayload));
    uint32_t now = micros();
    last_feedback_us = now;
    rtt_us = now - feedback.echo_t_us;
    have_feedback = true;
}

/** Drain one port into its parser with bulk reads, since a read per byte costs about 20 us. */
template <typename Port>
void poll(Port &port, FrameParser &parser) {
    uint8_t buf[128];
    int avail;
    while ((avail = port.available()) > 0) {
        size_t n = port.read(buf, min(size_t(avail), sizeof(buf)));
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            if (parser.feed(buf[i])) handle_frame(parser);
        }
    }
}

/** Write a frame only if it fits into the TX buffer, so a port nobody reads never blocks the loop. */
void send(Stream &port, const uint8_t *frame, size_t len) {
    if (port.availableForWrite() >= int(len)) port.write(frame, len);
}

/** True during the first half of each blink period. */
bool blink(uint32_t now_ms, uint32_t period_ms) {
    return now_ms % period_ms < period_ms / 2;
}

/** Red: solid kill switch, fast blink fault, slow blink servos not found. Green when nothing is wrong: solid torque on, slow blink host connected, short heartbeat without host. */
void update_leds(bool kill, bool fault, bool missing, bool timeout, bool enable) {
    uint32_t ms = millis();
    bool red = kill || (fault && blink(ms, LED_FAST_BLINK_MS)) || (missing && blink(ms, LED_SLOW_BLINK_MS));
    bool problem = kill || fault || missing;
    bool green = !problem && (enable || (!timeout && blink(ms, LED_SLOW_BLINK_MS)) ||
                              (timeout && ms % LED_HEARTBEAT_MS < LED_HEARTBEAT_ON_MS));
    digitalWrite(LED_RED_PIN, red ? HIGH : LOW);
    digitalWrite(LED_GREEN_PIN, green ? HIGH : LOW);
}

/** Switch from the stub to the real servos as soon as they answer, retrying every SERVO_RETRY_MS. */
void try_servos() {
    if (USE_STUB || servos == &xl330_servos) return;
    if (last_servo_try_ms != 0 && millis() - last_servo_try_ms < SERVO_RETRY_MS) return;
    last_servo_try_ms = millis() | 1;
    if (xl330_servos.begin()) servos = &xl330_servos;
}

void setup() {
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(KILL_PIN, INPUT_PULLUP);
    Serial.setRxBufferSize(1024);
    Serial.setTxTimeoutMs(0);
    Serial.begin(UART_BAUD);
    Serial0.setRxBufferSize(1024);
    Serial0.begin(UART_BAUD);
    stub_servos.begin();
    try_servos();
    next_state_us = micros();
}

void loop() {
    poll(Serial, usb_parser);
    poll(Serial0, uart_parser);
    try_servos();

    uint32_t now = micros();
    if (int32_t(now - next_state_us) < 0) return;
    next_state_us += STATE_PERIOD_US;
    if (int32_t(now - next_state_us) > int32_t(STATE_PERIOD_US)) next_state_us = now + STATE_PERIOD_US;

    bool kill = digitalRead(KILL_PIN) == (KILL_SWITCH_NC ? HIGH : LOW);
    bool timeout = !have_feedback || now - last_feedback_us > FEEDBACK_TIMEOUT_US;
    bool fault = servos->fault();
    bool missing = !USE_STUB && servos->is_stub();
    bool enable = !kill && !timeout && !fault && (feedback.flags & FEEDBACK_ENABLE);

    StatePayload state{};
    state.version = PROTOCOL_VERSION;
    float measured[NUM_CHANNELS];
    servos->read(state.closure, measured);

    float torque_cmd[NUM_CHANNELS];
    shaper.update(state.closure, feedback.torque, feedback.damping, feedback.slew, enable, STATE_PERIOD_US * 1e-6f,
                  torque_cmd);
    servos->write(torque_cmd, enable);

    memcpy(state.torque, measured, sizeof(measured));
    state.seq = ++state_seq;
    state.t_us = micros();
    state.rtt_us = rtt_us;
    state.flags = (kill ? STATE_KILL : 0) | (enable ? STATE_TORQUE_ON : 0) | (timeout ? STATE_TIMEOUT : 0) |
                  (servos->is_stub() ? STATE_STUB : 0) | (fault ? STATE_FAULT : 0);

    uint8_t frame[MAX_FRAME];
    size_t len = encode_frame(TYPE_STATE, &state, sizeof(state), frame);
    send(Serial, frame, len);
    send(Serial0, frame, len);

    update_leds(kill, fault, missing, timeout, enable);
}
