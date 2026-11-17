# Tactile

A standalone, one-handed tactile HID research device: **pebble or sphere**, built around two M5Stack **AtomS3R** (ESP32-S3-N8R8) modules. Motion, eventually capacitive fabric and force sensing, become low-latency intentional input; haptics express state without requiring a host application.

**v0.7.0 — direct speed dial with upright text.** This release isolates the
basic handling experiment on the battery-powered AtomS3R. Physical feel and
battery runtime require device testing; host tests cover the control law.

## Using the dial

1. Hold the screen facing you in a comfortable, roughly upright pose. After
   half a second of stillness that position becomes the dial centre.
2. Turn clockwise to read forward; anticlockwise to read backward. The angle
   directly sets speed. Holding a turned position keeps a constant speed.
3. Return to centre to pause on the current word. The stop region is ±6°;
   movement starts beyond ±8° to prevent jitter at its edge. Speed increases
   gently near centre and reaches ±3× at ±35° (440 ms/word at 1× by default).
4. To choose a new centre, lay the display flat, then lift it and settle again.
   The current word is retained. USB `reader centre` does the same recapture.

The word and small status labels continuously counter-rotate around the screen
centre. Gyro prediction and a gravity correction share one orientation estimate;
text upright is absolute, while dial centre is relative to your held pose.
Nearly horizontal screens have no reliable gravity-based "down" in their plane:
reading pauses there and resumes only after a new upright centre capture.

There is one short looping passage. Pitch entry/exit, focus dwell and the old
capability tutorial are suspended. Existing tutorial completion does not block
this release. Saved notes and configuration are not erased. BLE mouse remains
available in USB-selected diagnostics; the dial does not emit mouse commands.

After OTA, the display identifies this experiment as **v0.7 dial**. Wi-Fi
provisioning and the stable OTA polling mechanism are unchanged.

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
