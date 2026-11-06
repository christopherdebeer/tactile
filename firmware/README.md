# Firmware — AtomS3R

**Firmware 0.6.0 is the embodied-reader prototype.** It supports
the native USB console, LCD status screen, button state, Bosch BMI270/BMM150
initialization, 100Hz 9-axis telemetry, captive-portal Wi-Fi provisioning and
automatic stable HTTPS OTA with an A/B rollback-capable partition table. It
also advertises the experimental standard BLE gyro mouse on `pebble-01`;
capacitive Trill, pressure and haptics are not implemented yet.

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

## Embodied reader, button and display

- During the first five seconds after boot, hold for two seconds to start
  Wi-Fi setup. The display shows the `tactile-<device-id>` network and a new
  random password for that boot.
- During ordinary use, the display is a one-word-at-a-time narrative aligned
  around a fixed red recognition letter and fixation ticks. A clockwise roll
  displacement continuously increases the retained pace while held;
  counter-clockwise continuously decreases it through pause and then reverse.
  Returning upright freezes the selected pace. Pause for 400 ms to focus a
  word. Tilt the top edge away from the reader to enter a linked word and
  toward the reader to return to its parent.
- On first use, hold the display in a comfortable reading pose until it
  captures neutral. The tutorial then teaches faster, reverse, forward
  recovery, pause, focus, enter and exit. Pace lessons ignore the wrong
  direction, freeze at their proof threshold, and ask for an upright return.
  Pause cannot advance to focus until both roll and pitch are neutral. Tutorial
  completion persists; `reader tutorial restart` clears it and starts again.
- The mechanical button is behind the LCD and is intentionally unused by the
  reader because a hand pressing it hides the active word. The existing BLE
  mouse remains an experimental module but is not entered from the narrative.
- Join that network from a phone or computer. The captive portal should open
  automatically; otherwise browse to `http://192.168.4.1`.
- Select a scanned network or type its SSID, enter its password, and save. The
  list excludes detected Enterprise and other unsupported networks; selecting
  one manually is rejected when it was seen in the scan. The device stores
  accepted credentials in NVS and restarts.

The equivalent USB commands are `view reader`, `view debug`, `view telemetry`,
and `portal`.
Reader tuning commands are `reader pace <150-2000>`,
`reader invert-roll on|off`, and `reader invert-pitch on|off`; values persist
in NVS. `reader tutorial restart` replays the first-use capability gate.
Mouse tuning commands are `mouse axis x|y`, `mouse invert-y on|off`, and
`mouse gain <horizontal> <vertical>`; values persist in NVS. The
hardware-tested defaults are Z-axis horizontal rotation, inverted Y-axis
vertical rotation, and gains `22/28`.
The setup screen remains visible until credentials are saved and the device
restarts. If association fails afterward, the USB log includes the numeric and
named Wi-Fi disconnect reason; the diagnostic screen remains `WIFI OFF`.

The sensor loop remains 100Hz and supplies the reader at 50Hz. Rendering uses
a 16-bit off-screen frame buffer so a completed frame is transferred at once
without visible clear/redraw flicker. The reader redraws when its word or
coarsely quantized motion state changes, with a one-second keepalive. The
backlight starts below half brightness, dims after 30 seconds without a user
or radio-state event, and turns off after two minutes; connection events wake
it. Wi-Fi setup keeps the screen awake.

The reader derives absolute screen roll and top-edge pitch from the BMI270
accelerometer, relative to a comfortable pose captured after 400 ms of
stillness. A deliberate screen-plane rotation engages the analog pace control:
roughly 5° is neutral, about 30° reaches full input, and the retained signed
rate is limited to −2× through +4×. Gyro magnitude suppresses focus while the
hand is still moving and distinguishes a roll gesture from pickup or depth
tilt. Enter/exit require about 18° for 300 ms and must return within about 7°
of neutral before another depth gesture.
The experimental cursor continues to use only the BMI270 gyroscope. The
BMM150 remains available in the 100Hz telemetry stream but is not fused into
either interaction.

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
