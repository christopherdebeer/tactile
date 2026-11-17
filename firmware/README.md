# Firmware — AtomS3R

**Firmware 0.7.0 is the direct-dial reader experiment.** It retains native USB,
100Hz sensor acquisition, captive Wi-Fi setup, BLE mouse diagnostics and stable
HTTPS A/B OTA. No additional hardware sensors or haptics are required.

## Build and first USB flash

Tested with **ESP-IDF v5.3** on an AtomS3R with 8MB flash and 8MB octal PSRAM.

    mkdir -p ~/esp && cd ~/esp
    git clone -b release/v5.3 --recursive https://github.com/espressif/esp-idf.git
    cd esp-idf
    ./install.sh esp32s3
    . ./export.sh
    git clone https://github.com/christopherdebeer/tactile.git
    cd tactile/firmware
    idf.py set-target esp32s3
    idf.py build
    # Locate port (macOS): ls /dev/cu.usbmodem*
    idf.py -p /dev/cu.usbmodemXXXX flash monitor

After that, use the USB console:

    help
    status
    role pebble-01
    reader pace 440
    reader invert-roll off
    reader invert-pitch off
    reader tutorial restart
    mouse axis y
    mouse invert-y on
    mouse gain 22 28
    stream on
    mark rest
    stream off
    wifi MyNetwork MyPassword
    # Reboots. Wait for "Wi-Fi online".
    status
    ota

Use `role sphere-01` on the second unit. Credentials input does not support
spaces. Passwords remain in local NVS and are never printed by the firmware.
The stable JSONL contract and quality flags are documented in
`protocol/telemetry.md`.

## Direct dial, button and display

Hold the display comfortably upright and still for 500 ms to capture the dial
centre. Clockwise displacement reads forward; anticlockwise reads backward;
centre pauses. Angle sets signed rate directly, with no accumulated acceleration.
The detent stops inside 6° and releases beyond 8°. A quadratic response reaches
3× at 35° in either direction; `reader pace <150-2000>` sets base ms/word.
There are no pitch commands, links, focus dwell or mandatory tutorial.

The reader uses one looping passage. Lay flat then lift and settle to recapture
centre without losing the word. `reader centre` and the legacy
`reader tutorial restart` also recapture. Stored v0.6 tutorial completion is
ignored; no settings or notes are erased. `reader invert-roll on|off` changes
the dial direction only, never the text's upright direction. Pitch inversion
is retained for diagnostic compatibility but has no interaction effect.

Gyro prediction follows screen roll and gravity corrects drift. The same
estimate drives the dial and an independent absolute text counter-rotation.
Strong acceleration, missing samples and a nearly horizontal display pause
reading. Gravity cannot identify a viewer or distinguish a face-down screen
from a face-up one; this experiment assumes the screen faces its user.

The one word, fixed recognition anchor and signed rate are drawn into a second
128×128 RGB565 canvas. Antialiased rotation composites into the existing frame
before one LCD transfer. Text is inset to avoid clipping during rotation.
Quarter-degree angle changes request redraws; input is nominally 50Hz and actual
display latency/frame rate still need physical measurement.

The boot-time two-second button hold in the first five seconds still opens
Wi-Fi setup. Normal reading does not use the screen-button. USB `view debug`
and `view telemetry` expose the existing BLE mouse experiment. The sensor loop
continues at 100Hz; USB `stream on` includes dial, text rotation and word index.

Backlight dimming still follows frame/radio activity: 30 seconds to dim and
120 seconds to turn off. This is not a full battery power-management policy.

## OTA workflow

1. Initially flash via USB because the bootloader/partition table cannot be installed by application OTA.
2. Bump `firmware/version.txt` and merge an ESP-IDF-buildable change.
3. Tag `v0.1.1` and push. The release workflow builds a .bin and publishes `latest.json` in GitHub Releases.
4. Wi-Fi-connected devices automatically check the stable release manifest 30s after startup and every 30min, with no USB. The `ota` serial command requests an immediate check.
5. ESP-IDF streams the image to the inactive app partition and restarts. A trial build has 10s to survive early init, then the running firmware validates its slot.

Stable manifest: https://github.com/christopherdebeer/tactile/releases/latest/download/latest.json

**Limits:** We do not yet support beta/prerelease channels, enterprise Wi-Fi,
a remotely triggered immediate OTA check, a strong post-update health gate,
manifest hash verification or image signing. HTTPS and ESP-IDF image
validation are used, but that does **not** replace signing for higher-trust
fleet deployment. Wi-Fi isn't required for ordinary on-device operation.

## Recovery

If a firmware change breaks boot, ESP-IDF rollback can recover the previous slot **if the failed build never validated itself**. Validate this with a controlled broken-image experiment before relying on it.

Keep USB and ROM download access exposed in early enclosures. The AtomS3R can enter download mode through its download/reset procedure (see official M5Stack documentation). USB is the recovery route after corrupt partition changes or irrecoverable errors.

## Caveats

- Bosch's pinned BMI270 and BMM150 SensorAPI sources are vendored as ESP-IDF
  components with their BSD-3-Clause licenses and upstream commit IDs.
- M5GFX probes the AtomS3R display controller at startup and supports both shipped ST7735S and GC9107 variants.
- The AtomS3R does not include a LiPo charger. Use USB until a compatible power accessory is verified.
- Do not reuse Mochi's Waveshare board-specific pin definitions.
