# firmware

ESP32-S3 glove controller, PlatformIO with the Arduino framework. Speaks the glove protocol (`protocol/glove_serial.md`) at 500 Hz on both USB ports.

## Build and flash

Open this `firmware` folder in VS Code (PlatformIO needs `platformio.ini` at the workspace root), set `upload_port` / `monitor_port` in `platformio.ini`, then Upload. If it hangs at `Connecting...`: hold BOOT, tap RESET, release BOOT.

The serial monitor shows binary frames now, not text. Use `sim/tools/glove_monitor.py` instead and close it before the next upload, both need the port.

## Pins

| Pin | Use |
|---|---|
| GPIO 4 | status LED: on = host link alive, fast blink = kill switch, off = no host |
| GPIO 5 | kill switch to GND (internal pull-up), pressed = torque off |

Pins, rates, torque limit and stub settings are in `src/config.h`.

## Files

- `src/main.cpp` fixed-rate loop, watchdog, kill switch, LED
- `src/protocol.*` frames, CRC, parser
- `src/torque_shaper.*` contact damping, slew limit and clamping of the host torque (parameters arrive in every feedback frame)
- `src/servos.h` servo interface, `src/servos_stub.*` stand-in without hardware: mass-spring-damper operator fingers (stiffness 0.5 Nm per unit closure, 5 Hz, damping ratio 0.7) closing and opening every 3 s, pushed back by the applied torque

## Test without servos

```
cd sim
python tools/glove_monitor.py --port COM4 --torque 0.05
```

Prints rate, lost frames, round trip (mean, p99, max), closures and flags once per second. With the stub the closures settle torque / 0.5 lower while torque is on. Ground GPIO 5 and `torque on` should go away. `--out rtt.npy` saves all round trips.
