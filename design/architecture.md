# Tactile — architecture v0

**Device-independent interaction.** A standalone one-handed HID controller works offline. An optional experiment studio only helps interpret and refine human intent.

## On-device pipeline

Sensor acquisition (BMI270/BMM150, button, future Trill + force) -> timestamped observations -> conditioning -> explicit intention/gesture state machine -> standard BLE HID and haptic events.

USB/BLE diagnostic telemetry forms a **parallel** observation stream for session recording, labelling, deterministic replay and regression tests.

Management (device identity, Wi-Fi, OTA, rollback, configuration) is separate from latency-sensitive HID. Neither Wi-Fi nor Parc is needed for ordinary input.

## Reuse

- **Mochi ESP:** ESP-IDF build, NVS, Wi-Fi provisioning, A/B OTA, GitHub Releases manifests, rollback and idle-gated updates. Do not copy pet-specific APIs, Waveshare pinmaps or e-ink rendering.
- **SIMCAP/GAMBIT:** sensor stream semantics, event markers, collectors, labels, ML comparisons, MOUSE/KEYBOARD HID ideas. GAMBIT is a Puck.js/Espruino firmware, *not* drop-in ESP32 code.
- **SIMCAP lesson:** model accuracy is not meaningful until held-out real-world tests exist. Instrument raw capture and deterministic replay before training a classifier.

## Sensor limitations

Independent capacitive textile patches can map contact distributions but cannot guarantee finger identity at merged contacts. Touch capacitance alone is not calibrated force. Pressure uses separate sensing. Keep ball/pebble form an open experimental choice.

## Planned protocol

schema_version, device_id, firmware_version, seq, mono_us, accel_xyz, gyro_xyz, mag_xyz?, button, cap_raw[N]?, force_raw[M]?, flags.

P0 only probes BMI270 chip ID and does not implement this telemetry yet. P1 must make this contract explicit before any trained inference.
