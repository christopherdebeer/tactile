# Tactile

A standalone, one-handed tactile HID research device: **pebble or sphere**, built around two M5Stack **AtomS3R** (ESP32-S3-N8R8) modules. Motion, eventually capacitive fabric and force sensing, become low-latency intentional input; haptics express state without requiring a host application.

**Status (2026-11-06): M1 is hardware-validated on `pebble-01`.** Native USB,
8MB PSRAM, the LCD, Bosch BMI270 configuration, BMM150 auxiliary hub and 100Hz
9-axis JSONL stream have been verified on hardware. M1 adds a standard BLE
gyro mouse with hold-to-move clutching and tap-to-click on the Atom button.
The full labelled motion experiment and `sphere-01` comparison are deferred
until battery power is available.

## Afterword on the Atom (v0.5.0)

Afterword starts on the LCD and cycles through six short phrases. After the
five-second Wi-Fi setup window, tap the screen-button to enter the phrase in
focus. Hold and twist around the axis through the screen to browse. Keep it
turned for repeated steps, unwind to stop, and release to choose.

- **Step inside** opens the interior world (below).
- **Motion** responds to rotation. Tap to return.
- **Reading pace** previews each cadence. Release on a pace to save it.
- **Note** reads a saved note in chunks. Tap to edit, or hold and turn to
  return. In the editor choose a letter group and letter, borrow a device
  word, insert a space, erase, or move the insertion cursor. Save commits up
  to 95 lowercase ASCII characters and spaces to NVS; discard restores the
  previous note. Twist in either direction to reach save and discard.
- **Pointer** enables the BLE mouse clutch and tap-click. Hold still for
  2.2 seconds and release to return to Afterword.
- **About** shows firmware version and connectivity. Tap to return.

### Interior world (v0.5.0)

Afterword is the starting point for Tactile's primary meta-control; the BLE
pointer is one capability beneath it. The interior is an experiment in
exploring and manipulating a small world through the hand, one primary word at
a time, with tertiary detail lines above and below.

A house of four places: **hall** (bell; doors to study, garden, stair and
outward), **study** (lamp, letter), **garden** (seed, bowl) and **cellar**
(stone, dark until the lamp is lit). Each place and thing first *tells*
itself word by word at the saved reading pace, then rests on its name.

- **Hold + twist** turns through what is here; **release** goes through a door
  or takes up a thing. `outward` in the hall returns to Afterword.
- **Tip** the device away from how you have been holding it to look closer:
  the neighbour lines become a detail (lamp `glowing`, stair `cellar`). The
  rest pose adapts within a few seconds, so any comfortable grip works.
- **Tap** sets down the thing you hold.

Each thing answers a different sensor channel, so the world doubles as a
catalogue of candidate gestures for single-button meta-control:

| Thing | Channel | Effect |
| --- | --- | --- |
| lamp | twist while held (gyro, screen axis) | winds it; fades over ~4 min; lights the cellar |
| letter | twist while held; otherwise RSVP | reads and scrubs the saved Afterword note |
| bowl | tilt relative to the pose it was taken up in | pours into the garden soil; rain refills it |
| seed | stillness (< 0.15 rad/s) | grows seed → sprout → shoot → bud → flower while the soil is wet |
| bell | shake or flick (> 6 rad/s) | rings; the house remembers how often |
| stone | button hold duration | warms while held, cools when released |

World state (lamp, water, soil, growth, warmth, rings) evolves while you are
inside and persists to NVS as `aw_world`, at most once a minute and whenever a
thing is set down or you step outward. Thresholds are first guesses to be
tuned on `pebble-01`; the tip direction is deliberately sign-agnostic until
the axis conventions are confirmed on hardware.

USB commands `view debug`, `view telemetry` and `view afterword` switch
between the diagnostic and reading interfaces. The existing boot-hold Wi-Fi
setup and stable-release OTA poll continue to work.

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
