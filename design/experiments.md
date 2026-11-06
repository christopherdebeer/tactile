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

## Embodied narrative — temporal navigation (v0.6.0 prototype)
- The device is the narrative; there is no menu entry or separate interior.
  The hall is the root passage and the display shows exactly one word.
- Grammar under test: each screen-facing clockwise/counter-clockwise excursion
  continuously changes a retained signed pace while displaced; returning
  upright freezes it. The pause state focuses a word; top edge away enters its
  linked passage; top edge toward the reader exits to the parent word. The
  screen-button is not part of ordinary reading.
- Initial thresholds: configured base pace 150–2000 ms/word (440 ms default);
  roll has a roughly 5° dead zone and reaches full analog input around 30°;
  signed rate slews at up to 1.6× per second and is bounded at −2×/+4×; focus
  follows 400 ms paused; enter/exit require a 300 ms tilt beyond about 18°
  with a neutral re-arm inside about 7°.
- First-use capability gate: capture neutral, increase pace, reach reverse,
  recover forward, pause, focus a known word, enter, exit, and return upright.
  Each pace lesson ignores the opposite direction and freezes after proving
  its target. Pause must return to neutral in both roll and pitch before the
  depth lessons begin. Completion persists and can be replayed from USB.
- Observe on `pebble-01`: confirm perceived roll and pitch signs; determine
  whether the pause band can be held without wrist strain; measure accidental
  enter/exit events; tune acceleration, reverse reach and dwell times.
- Host regression: `tests/afterword.c` covers retained relative pace,
  pause/focus, linked entry, exact-parent exit, reverse reading, invalid
  orientation, axis inversion and RSVP anchor placement. CI runs it before
  every firmware build and release.

Hardware observation on `pebble-01` (2026-11-06): clockwise increase,
counter-clockwise reverse, forward recovery and pause were demonstrated with
the device tethered. The initial tutorial allowed a small wrong-way resting
bias to alter pace and allowed focus to begin with approximately 16.6° of
residual pitch; both false-positive paths are now regression-covered and
flashed. A clean enter/exit replay on the refined build remains open.

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
