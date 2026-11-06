# Firmware — AtomS3R

**Firmware 0.2.2 is the current AtomS3R build.** It supports
the native USB console, LCD status screen, button state, Bosch BMI270/BMM150
initialization, 100Hz 9-axis telemetry, captive-portal Wi-Fi provisioning and
automatic stable HTTPS OTA with an A/B rollback-capable partition table. BLE
HID, capacitive Trill, pressure and haptics are not implemented yet.

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

## Button and display

- Short press and release: toggle between the diagnostic status card and a
  live view of acceleration, angular velocity and magnetic-field magnitude.
- Hold for three seconds: start Wi-Fi setup. The display shows the
  `tactile-<device-id>` network and a new random password for that boot.
- Join that network from a phone or computer. The captive portal should open
  automatically; otherwise browse to `http://192.168.4.1`.
- Select a scanned network or type its SSID, enter its password, and save. The
  list excludes detected Enterprise and other unsupported networks; selecting
  one manually is rejected when it was seen in the scan. The device stores
  accepted credentials in NVS and restarts.

The equivalent USB commands are `view debug`, `view telemetry`, and `portal`.
The setup screen remains visible until credentials are saved and the device
restarts. If association fails afterward, the USB log includes the numeric and
named Wi-Fi disconnect reason; the diagnostic screen remains `WIFI OFF`.

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
