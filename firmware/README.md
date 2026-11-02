# firmware

ESP32-S3 glove controller, PlatformIO with the Arduino framework. Speaks the glove protocol (`protocol/glove_serial.md`) at 500 Hz on both USB ports.

## Build and flash

Open this `firmware` folder in VS Code (PlatformIO needs `platformio.ini` at the workspace root), set `upload_port` / `monitor_port` in `platformio.ini`, then Upload. If it hangs at `Connecting...`: hold BOOT, tap RESET, release BOOT.

The serial monitor shows binary frames now, not text. Use `sim/tools/glove_monitor.py` instead and close it before the next upload, both need the port.

## Pins

| Pin | Use |
|---|---|
| GPIO 4 | green LED through 330 Ohm to GND |
| GPIO 5 | kill switch to GND (internal pull-up); `KILL_SWITCH_NC` picks normally open or normally closed |
| GPIO 6 | red LED through 330 Ohm to GND |
| GPIO 17 | servo bus TX, through 1 kOhm to the servo DATA line |
| GPIO 18 | servo bus RX, directly on the servo DATA line |

Servos: 3x XL330-M077-T on a 5 V supply whose GND is shared with the ESP. All DATA wires, the resistor end and GPIO 18 meet in one breadboard row. Do not use the board pins labeled TX/RX (GPIO 43/44, UART0 to the USB bridge).

Pins, rates, torque limit, servo mapping and stub settings are in `src/config.h`.

## Files

- `src/main.cpp` fixed-rate loop, watchdog, kill switch, LED
- `src/protocol.*` frames, CRC, parser
- `src/torque_shaper.*` contact damping, slew limit and clamping of the host torque (parameters arrive in every feedback frame), soft start over `ENABLE_RAMP_S` whenever torque is enabled again (kill switch released, link back, fault cleared, sim engaged), so a finger closed deep into an object is not snapped open
- `src/dxl_bus.*` Dynamixel protocol 2.0 on the half-duplex bus without buffer chip (echo discarded, bulk reads, sync read/write)
- `src/servos_xl330.*` real servos: current mode, closure from position, resisting torque to goal current, no torque below open and a fade-in over the first 5 % of closure (`CLOSURE_FADE_IN`), so the push never switches on in one step; beyond closed it keeps pushing toward open, fade above 55 C, bus watchdog 100 ms, fault on a lost servo or hardware alert with automatic re-init or reboot
- `src/servos.h` servo interface, `src/servos_stub.*` stand-in without hardware: mass-spring-damper operator fingers (stiffness 0.5 Nm per unit closure, 5 Hz, damping ratio 0.7) closing and opening every 3 s, pushed back by the applied torque

## Status LEDs

Red means something is wrong, green means all is well; solid means active, blinking means waiting.

| Red | Green | State |
|---|---|---|
| solid | off | kill switch pressed |
| fast blink | off | servo fault: a servo lost or reporting a hardware error, torque off until it recovers |
| slow blink | off | servos not found yet, running the stub (retries every second) |
| off | solid | torque on, sim hand engaged |
| off | slow blink | host connected, waiting (sim hand not engaged or stopped) |
| off | short flash every 2 s | no host |

## Kill switch

A small two-contact button between GPIO 5 and GND works with `KILL_SWITCH_NC = false`. With a three-pin button module, use only its switch contacts (not its VCC/OUT electronics); never put 5 V on a GPIO. For the final glove use a normally closed emergency button and set `KILL_SWITCH_NC = true`: pressing it or a broken wire then both stop the torque.

## Servo setup

The servos need IDs 1-3, 1 Mbps and Return Delay 0 in their EEPROM (done once with the bench sketches `servo_id` and `servo_bus`). The firmware pings IDs 1-3 at boot and then every second until all three answer; until then it runs the stub and sets the STUB flag, so the servo supply can be switched on before or after the ESP. `USE_STUB = true` forces the stub.

Per channel in `config.h`: `SERVO_IDS` (channel order thumb, index, middle), `SERVO_CLOSE_DIR`, open position and `SERVO_RANGE_DEG`. Without the glove mechanics `SERVO_OPEN_AT_BOOT = true` takes the horn position at the moment the servos are found (ESP reset or servo supply on) as open, and closed is `SERVO_RANGE_DEG` further in the closing direction. `SERVO_TORQUE_NM_PER_A` is an estimate until measured. `SERVO_CURRENT_LIMIT_MA` should match the Current Limit in the servo EEPROM.

## Test with servos, without mechanics

1. Put all horns where "open" should be, then switch on the servo supply (or reset the ESP if it is already on).
2. `python tools/glove_monitor.py --port COM4 --torque 0.05`: flags show no stub, closures follow the horns when turned by hand (0 at boot position, 1 after `SERVO_RANGE_DEG`). If a closure goes negative when closing, flip that channel's `SERVO_CLOSE_DIR`.
3. With torque on the horn pushes toward open; a free horn turns back to open and the push fades out over the last 5 % before it. Hold the horn and close it by hand to feel the push. `--torque 0` first to just check that closures follow the horns. Ground GPIO 5: the push stops at once.
4. Sim with finger source `glove`: turn the horns to close the sim fingers on an object and feel the contact torque.

## Safety tests

Run each during contact with the sim running and check the log (`glove_torque_*`, `glove_flags`) afterwards.

| Test | Expected |
|---|---|
| Press the kill switch | torque 0 within one cycle, red solid; on release torque fades back in over 0.8 s |
| Stop the sim (`s`) or close it | torque 0 within 50 ms (feedback timeout), green slow blink or heartbeat |
| Reset the ESP or unplug its USB | servos stop within 100 ms (bus watchdog) |
| Unplug one servo's data or power | fault within 50 ms, all torque 0, red fast blink; plug it back in and it recovers by itself. A lost servo can make the servos after it in the ID order look lost as well, since sync read replies come in ID order. |

## Test without servos

```
cd sim
python tools/glove_monitor.py --port COM4 --torque 0.05
```

Prints rate, lost frames, round trip (mean, p99, max), closures and flags once per second. With the stub the closures settle torque / 0.5 lower while torque is on. Ground GPIO 5 and `torque on` should go away. `--out rtt.npy` saves all round trips.
