# Experiments and acceptance

## P0 — USB bootstrap and OTA
- Flash the two AtomS3Rs over USB for the first time, give them roles pebble-01 and sphere-01.
- Confirm distinct identity, USB console, button events, chip ID probe, Wi-Fi connectivity and version.
- Publish a newer stable GitHub release. Confirm both devices update without USB tether.
- Deliberately test a broken trial image and rollback, plus ROM USB recovery.
- Offline execution must remain possible when Wi-Fi is unavailable.
- **Nothing is considered hardware-validated until these steps have actually run.**

`pebble-01` OTA result (2026-11-06): the published `v0.3.0` release workflow
completed successfully. A verified v0.2.2 application was placed in `ota_0`;
the device downloaded v0.3.0 over Guest Wi-Fi into `ota_1`, booted from
`0x310000`, passed the ten-second health window and validated the new slot.
Role, Wi-Fi credentials, BLE bond and mouse tuning survived. The second-device
upgrade and deliberate rollback/recovery drills remain open.

## P1 — Raw 9-axis telemetry and reproducibility
- [x] Bosch BMI270 initialization/configuration plus BMM150 auxiliary sensor hub
  validated on `pebble-01`.
- [x] Monotonic sample timestamps at 100Hz, sequence numbers and missing-sample
  accounting validated on a stationary live stream.
- Record rest, wrist reposition, deliberate roll, flick, grip and button clutch.
- [x] Raw JSONL recorder and deterministic reference button-clutch replay.
- [ ] Run and retain the full labelled physical-motion session set after the
  battery arrives; the USB cable changes the motion mechanics.

## M1 — `pebble-01` BLE clutch mouse vertical slice
- Pair to a computer as `Tactile pebble-01`, using standard BLE HID with no
  host application.
- With the button released, motion must produce no pointer reports.
- With the button held after the five-second boot window, rotation must move
  the pointer; release must stop it immediately.
- A quick tap must emit one standard left-button click without requiring a
  host application.
- Confirm Wi-Fi and BLE coexist, the 100Hz sensor loop remains alive, and the
  display remains stable at 20fps while clutched and 2fps while idle.
- USB supplies power and diagnostics only for M1. Do not treat tethered motion
  traces as the final labelled dataset.

Hardware result on `pebble-01` (2026-11-06): BLE pairing, gyro motion,
released-clutch gating, tap-to-click, Guest Wi-Fi coexistence, persistent
Z/Y-inverted mapping and `22/28` gain were exercised successfully. Dragging
and right-click remain outside the one-button M1 mapping.

## Meta-control exploration — Afterword interior (v0.5.0)
- Treat Afterword as the primary meta-control; the BLE pointer is its first
  capability. The interior world probes which sensor channels can carry
  simple, expressive control with one button: twist-while-held, tilt relative
  to an adaptive rest pose, stillness, shake/flick and hold duration.
- Grammar under test: hold + twist browses, release chooses, tap backs out of
  a held thing, tip looks closer (tertiary detail), and leaving is a place
  (`outward`) rather than a reserved gesture.
- Observe on `pebble-01`: does tipping to look closer fire during ordinary
  handling? Does a set-down tap disturb the thing it releases? Does stillness
  growth or a shake ring happen by accident? Which channels feel intentional?
- Host regression: `tests/world.c` walks the house, lights the lamp, pours
  the bowl, grows the seed, warms the stone, rings the bell and checks
  persistence. CI runs it before every firmware build and release.
- Not yet measured on hardware; thresholds are provisional.

## P2 — Quantitative BLE HID
- Pair to a computer as a conventional HID mouse/keyboard with no host app.
- Measure rest jitter, unintended movement per minute, clutch latency and repeatability.
- Compare sphere and pebble with the same firmware.

## P3 — Capacitive textile
- Trill Craft powered and signalled at compatible voltage via separate external I2C.
- Begin with 8 isolated foil/fabric electrodes; expand to 15+ based on response.
- Characterize touch count resolution, drift, hand size, humidity, and electrode coupling.

## P4 — Pressure and haptics
- Squeeze sensing through a separate resistive/force or pressure-capacitive layer.
- LRA haptic events without motion-triggered false positives or magnetic sensor interference.
- Compare removable foam-clay/soft-skin pebble and sphere enclosures.
