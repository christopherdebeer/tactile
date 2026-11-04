# Experiments and acceptance

## P0 — USB bootstrap and OTA
- Flash the two AtomS3Rs over USB for the first time, give them roles pebble-01 and sphere-01.
- Confirm distinct identity, USB console, button events, chip ID probe, Wi-Fi connectivity and version.
- Publish a newer stable GitHub release. Confirm both devices update without USB tether.
- Deliberately test a broken trial image and rollback, plus ROM USB recovery.
- Offline execution must remain possible when Wi-Fi is unavailable.
- **Nothing is considered hardware-validated until these steps have actually run.**

## P1 — Raw 9-axis telemetry and reproducibility
- Bosch BMI270 initialization/configuration plus BMM150 auxiliary sensor hub.
- Monotonic sample timestamps (target 50-100Hz), sequence numbers, missing-sample accounting.
- Record rest, wrist reposition, deliberate roll, flick, grip and button clutch.
- Deterministic replay must reproduce interpretation state transitions.

## P2 — Standard BLE HID
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
