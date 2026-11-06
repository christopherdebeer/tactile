# Tactile

A standalone, one-handed tactile HID research device: **pebble or sphere**, built around two M5Stack **AtomS3R** (ESP32-S3-N8R8) modules. Motion, eventually capacitive fabric and force sensing, become low-latency intentional input; haptics express state without requiring a host application.

**Status (2026-11-06): M1 is hardware-validated on `pebble-01`.** Native USB,
8MB PSRAM, the LCD, Bosch BMI270 configuration, BMM150 auxiliary hub and 100Hz
9-axis JSONL stream have been verified on hardware. M1 adds a standard BLE
gyro mouse with hold-to-move clutching and tap-to-click on the Atom button.
The full labelled motion experiment and `sphere-01` comparison are deferred
until battery power is available.

## Architecture

- **Device** — ESP-IDF firmware, USB diagnostics, Wi-Fi OTA, eventually BLE HID and haptics. Standard HID must work without Wi-Fi or a connected studio.
- **Studio** — session capture, labels, replay, timing and error measurement. Inspired by [SIMCAP/GAMBIT](https://github.com/christopherdebeer/simcap).
- **Lifecycle** — A/B firmware partitions, boot validation and OTA, taking patterns from [Mochi ESP](https://github.com/christopherdebeer/mochi-esp), but no pet/backend dependency.
- **Parc** — the linked project knowledge graph and tasks: `kb/proj_tactile_hid`, starting experiment `experiment/tactile-hid/p0`.

## Current firmware

The firmware retains the recoverable P0 spine—native USB console, button input,
LCD status, Wi-Fi credentials, stable HTTPS OTA, A/B application partitions
and rollback—and adds the P1 BMI270/BMM150 acquisition path plus the M1 BLE
mouse vertical slice. It streams
transport-neutral observations with monotonic timestamps, sequence numbers,
missing-sample accounting and markers. After the five-second boot setup window,
holding the button clutches in gyro-relative mouse motion and releasing it
stops motion; a quick tap sends a left click while BLE is connected. When BLE
is disconnected, a tap toggles diagnostics and live telemetry. Holding the
button for two seconds during boot starts on-device Wi-Fi provisioning.

See [firmware/README.md](firmware/README.md) for installation/commands; see [design/architecture.md](design/architecture.md) for system boundaries, [design/experiments.md](design/experiments.md) for acceptance tests, and [hardware/atoms3r.md](hardware/atoms3r.md) for board-specific wiring.

Capture a five-second session:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/capture.py --port /dev/cu.usbmodem101 \
  --duration 5 --marker rest --output sessions/rest.jsonl \
  --transitions sessions/rest.transitions.jsonl
python3 tools/replay.py sessions/rest.jsonl \
  --transitions sessions/rest.replayed.transitions.jsonl
```

## Milestones

1. **P0:** bootstrap both AtomS3Rs by USB, validate Wi-Fi/manual OTA and a failed-boot recovery drill; identify them independently.
2. **P1:** BMI270 + BMM150 raw telemetry with sequence numbers, timestamps, quality flags, a recorder and replayable viewer.
3. **M1:** `pebble-01` standard BLE gyro mouse, physical clutch and a no-motion-when-released proof while USB supplies power.
4. **P2:** quantitative intentional-vs-repositioning baseline and pebble-versus-sphere comparison after battery delivery.
5. **P3:** Trill Craft and isolated conductive-fabric electrodes (multi-contact presence/position; not per-finger force).
6. **P4:** force sensing, haptics, and pebble-versus-sphere ergonomic studies.

Never put Wi-Fi passwords or device-specific secrets into Git.
