# AtomS3R hardware references

Two M5Stack AtomS3Rs delivered on 2026-11-04. The first device was flashed on
2026-11-05 and assigned `pebble-01`; USB, PSRAM, BMI270 configuration, BMM150
auxiliary acquisition, 100Hz telemetry and its LCD status screen passed
hardware checks.

Authoritative pins: https://docs.m5stack.com/en/core/AtomS3R

| Signal | Board pin | Notes |
| --- | --- | --- |
| SYS I2C clock | GPIO0 | BMI270 and LP5562 shared bus, I2C controller 1 |
| SYS I2C data | GPIO45 | BMI270 at 0x68, chip ID 0x24 |
| User button | GPIO41 | Active low; tap changes view, 3s hold starts Wi-Fi setup |
| BMM150 | BMI270 auxiliary I2C | Not a standalone SYS I2C address |
| Grove yellow | GPIO2 | Future external sensor SDA |
| Grove white | GPIO1 | Future external sensor SCL |
| Grove red | **5V** | Never use directly for 3.3V-only Trill |
| USB-C | Native USB Serial/JTAG | First flash, debug, recovery |

The LCD is 128x128 and shipped AtomS3R batches may contain either ST7735S or GC9107 controllers. Firmware uses M5GFX runtime detection, with SPI clock GPIO15, MOSI GPIO21, CS GPIO14, DC GPIO42 and reset GPIO48. The LP5562 backlight controller is at internal I²C address 0x30.

AtomS3R includes neither a LiPo battery nor a LiPo charge circuit. Prototype on USB before choosing a compatible battery base. Keep magnetic haptic actuators away from the BMM150 or characterize their interference.
