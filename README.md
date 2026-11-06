# Tactile

A standalone, one-handed tactile HID research device: **pebble or sphere**, built around two M5Stack **AtomS3R** (ESP32-S3-N8R8) modules. Motion, eventually capacitive fabric and force sensing, become low-latency intentional input; haptics express state without requiring a host application.

**Status (2026-11-06): M1 is hardware-validated on `pebble-01`.** Native USB,
8MB PSRAM, the LCD, Bosch BMI270 configuration, BMM150 auxiliary hub and 100Hz
9-axis JSONL stream have been verified on hardware. M1 proved a standard BLE
gyro mouse with hold-to-move clutching and tap-to-click; the v0.6 prototype
now uses gravity-derived roll and pitch for a button-free embodied reader.
The full labelled motion experiment and `sphere-01` comparison are deferred
until battery power is available.

## Embodied reader (v0.6.0 prototype)

The device and its narrative are the same thing. There is no menu and no
separate “interior”: when the display is facing the reader, it continuously
speaks the current passage one word at a time.

- The current signed pace is retained while the display is upright.
- Rotate the display clockwise like a dial and keep it displaced to
  continuously increase the pace. Return upright to retain the new pace.
- Rotate counter-clockwise to continuously decrease the pace through a
  stopped band and into reverse. The angle is an input direction and
  intensity, not an absolute mapping from pose to pace.
- Remain paused for 400 ms to focus the visible word.
- Tilt the top edge away from the reader for 300 ms to enter a focused linked
  word.
- Tilt the top edge toward the reader for 300 ms to return to the exact word
  in the parent passage. Return near neutral before another tilt can fire.

On first use, a capability tutorial captures the reader's comfortable upright
pose, then teaches faster, reverse, recovery to forward, pause, focus, enter
and exit in that order. A pace lesson accepts only the direction it is
teaching, freezes when its target is demonstrated, and advances only after the
device returns upright. The transition from pause to the depth gestures
requires both roll and pitch to return to the captured pose, preventing an
already-tilted hand from accidentally satisfying “enter.” Completion persists
in NVS; `reader tutorial restart` replays the tutorial.

The root passage is the hall itself. Words such as `bell`, `study`, `garden`
and `stairs` open deeper passages; linked words in those passages open the
lamp, letter, seed, bowl and stone narratives. Leaf passages loop until the
reader tilts back. A saved note, when present, is the letter's narrative.

The mechanical button sits behind the display, so ordinary reading does not
use it: pressing would obscure the word being read. Its boot-time two-second
hold remains the recoverable Wi-Fi provisioning path. USB diagnostics remain
available through `view reader`, `view debug` and `view telemetry`.

The first physical-axis assumptions are deliberately configurable:
`reader pace <150-2000>`, `reader invert-roll on|off`, and
`reader invert-pitch on|off`. These settings persist in NVS. The non-inverted
defaults match clockwise-faster and top-edge-away-to-enter on `pebble-01`.

Each word is laid out around a fixed recognition anchor slightly left of its
geometric centre. The anchor letter is red, with fixation ticks above and
below, so differently sized words do not pull the eye across the display.

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
missing-sample accounting and markers. The embodied reader is now the default
display interaction; the previous button-clutched pointer remains compiled as
an experimental capability but is not entered from the narrative. Holding the
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
