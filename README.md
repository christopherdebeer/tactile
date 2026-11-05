# Tactile

A standalone, one-handed tactile HID research device: **pebble or sphere**, built around two M5Stack **AtomS3R** (ESP32-S3-N8R8) modules. Motion, eventually capacitive fabric and force sensing, become low-latency intentional input; haptics express state without requiring a host application.

**Status (2026-11-05): P1 telemetry is running on `pebble-01`.** Native USB,
8MB PSRAM, the LCD, Bosch BMI270 configuration, BMM150 auxiliary hub and 100Hz
9-axis JSONL stream have been verified on hardware. The full labelled motion
experiment set is still outstanding.

## Architecture

- **Device** — ESP-IDF firmware, USB diagnostics, Wi-Fi OTA, eventually BLE HID and haptics. Standard HID must work without Wi-Fi or a connected studio.
- **Studio** — session capture, labels, replay, timing and error measurement. Inspired by [SIMCAP/GAMBIT](https://github.com/christopherdebeer/simcap).
- **Lifecycle** — A/B firmware partitions, boot validation and OTA, taking patterns from [Mochi ESP](https://github.com/christopherdebeer/mochi-esp), but no pet/backend dependency.
- **Parc** — the linked project knowledge graph and tasks: `kb/proj_tactile_hid`, starting experiment `experiment/tactile-hid/p0`.

## Current firmware

The firmware retains the recoverable P0 spine—native USB console, button input,
LCD status, Wi-Fi credentials, stable HTTPS OTA, A/B application partitions
and rollback—and adds the P1 BMI270/BMM150 acquisition path. It streams
transport-neutral observations with monotonic timestamps, sequence numbers,
missing-sample accounting and markers. It does not yet provide a captive
portal or advertise BLE HID.

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
3. **P2:** BLE HID, clutch and quantitative intentional-vs-repositioning baseline.
4. **P3:** Trill Craft and isolated conductive-fabric electrodes (multi-contact presence/position; not per-finger force).
5. **P4:** force sensing, haptics, and pebble-versus-sphere ergonomic studies.

Never put Wi-Fi passwords or device-specific secrets into Git.
