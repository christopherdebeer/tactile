# Firmware — AtomS3R

**P0 is committed source, not yet hardware-tested.** It supports the native USB console, BMI270 chip-ID probe, button count, Wi-Fi configuration in NVS and automatic stable HTTPS OTA with an A/B rollback-capable partition table. The display, sensor streaming, BLE HID, capacitive Trill, pressure and haptics are deliberately not implemented yet.

## Build and first USB flash

Tested source target: **ESP-IDF v5.3**, not yet verified against actual hardware.

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
    wifi MyNetwork MyPassword
    # Reboots. Wait for "Wi-Fi online".
    status
    ota

Use `role sphere-01` on the second unit. P0 credentials input doesn't support spaces. Passwords remain in local NVS and are never printed by the firmware.

## OTA workflow

1. Initially flash via USB because the bootloader/partition table cannot be installed by application OTA.
2. Bump `firmware/version.txt` from e.g. `0.1.0` to `0.1.1` and merge an ESP-IDF-buildable change.
3. Tag `v0.1.1` and push. The release workflow builds a .bin and publishes `latest.json` in GitHub Releases.
4. Wi-Fi-connected devices automatically check the stable release manifest 30s after startup and every 30min, with no USB. The `ota` serial command requests an immediate check.
5. ESP-IDF streams the image to the inactive app partition and restarts. A trial build has 10s to survive early init, then the running firmware validates its slot.

Stable manifest: https://github.com/christopherdebeer/tactile/releases/latest/download/latest.json

**Limits:** We do not yet support beta/prerelease channels, a browser provisioning portal, a remotely triggered immediate OTA check, a strong post-update health gate, manifest hash verification or image signing. HTTPS and ESP-IDF image validation are used, but that does **not** replace signing for higher-trust fleet deployment. Wi-Fi isn't required for ordinary on-device operation.

## Recovery

If a firmware change breaks boot, ESP-IDF rollback can recover the previous slot **if the failed build never validated itself**. Validate this with a controlled broken-image experiment before relying on it.

Keep USB and ROM download access exposed in early enclosures. The AtomS3R can enter download mode through its download/reset procedure (see official M5Stack documentation). USB is the recovery route after corrupt partition changes or irrecoverable errors.

## Caveats

- P0 only reads the BMI270 ID register; Bosch sensor configuration data and high-rate BMI270/BMM150 streaming are P1.
- The AtomS3R's display controller revision must be checked before LCD initialization. P0 runs without it.
- The AtomS3R does not include a LiPo charger. Use USB until a compatible power accessory is verified.
- Do not reuse Mochi's Waveshare board-specific pin definitions.
