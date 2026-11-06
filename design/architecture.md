# Tactile — architecture v0

**Device-independent interaction.** A standalone one-handed HID controller works offline. An optional experiment studio only helps interpret and refine human intent.

## On-device pipeline

Sensor acquisition (BMI270/BMM150, button, future Trill + force) -> timestamped observations -> conditioning -> explicit intention/gesture state machine -> standard BLE HID and haptic events.

USB/BLE diagnostic telemetry forms a **parallel** observation stream for session recording, labelling, deterministic replay and regression tests.

Management (device identity, Wi-Fi, OTA, rollback, configuration) is separate from latency-sensitive HID. Neither Wi-Fi nor Parc is needed for ordinary input.

The v0.6 interaction is an on-device embodied reader rather than a menu.
Gravity establishes roll and pitch relative to a settled reading pose; gyro
motion gates intentional roll, focus and depth gestures. Roll continuously
adjusts a retained signed RSVP rate, pause focuses the current word, and pitch
enters or exits linked passages. The mechanical screen-button is reserved for
recoverable boot-time provisioning because pressing it obscures the display.
A persisted first-use tutorial acts as a capability gate and exposes its state
in the observation stream.

## Reuse

- **Mochi ESP:** ESP-IDF build, NVS, Wi-Fi provisioning, A/B OTA, GitHub Releases manifests, rollback and idle-gated updates. Do not copy pet-specific APIs, Waveshare pinmaps or e-ink rendering.
- **SIMCAP/GAMBIT:** sensor stream semantics, event markers, collectors, labels, ML comparisons, MOUSE/KEYBOARD HID ideas. GAMBIT is a Puck.js/Espruino firmware, *not* drop-in ESP32 code.
- **SIMCAP lesson:** model accuracy is not meaningful until held-out real-world tests exist. Instrument raw capture and deterministic replay before training a classifier.

## Sensor limitations

Independent capacitive textile patches can map contact distributions but cannot guarantee finger identity at merged contacts. Touch capacitance alone is not calibrated force. Pressure uses separate sensing. Keep ball/pebble form an open experimental choice.

## Observation protocol

P1 implements the explicit JSONL observation contract in
`protocol/telemetry.md`: identity and firmware envelope, sequence and monotonic
time, SI-unit 9-axis data, button state, future sensor vectors and quality
flags. Raw capture is immutable. Derived button-clutch transitions live in a
separate versioned stream, establishing the deterministic replay boundary
before trained inference.
