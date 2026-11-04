# AtomS3R hardware references

Two M5Stack AtomS3Rs delivered on 2026-11-04. No device has been flashed by this project.

Authoritative pins: https://docs.m5stack.com/en/core/AtomS3R

| Signal | Board pin | Notes |
| --- | --- | --- |
| SYS I2C clock | GPIO0 | BMI270 and LP5562 shared bus |
| SYS I2C data | GPIO45 | BMI270 at 0x68, chip ID 0x24 |
| User button | GPIO41 | Under LCD; active low |
| BMM150 | BMI270 auxiliary I2C | Not a standalone SYS I2C address |
| Grove yellow | GPIO2 | Future external sensor SDA |
| Grove white | GPIO1 | Future external sensor SCL |
| Grove red | **5V** | Never use directly for 3.3V-only Trill |
| USB-C | Native USB Serial/JTAG | First flash, debug, recovery |

The LCD is 128x128; newer revisions use ST7735 (older examples may use GC9107). The display is deliberately deferred until board-specific initialization is confirmed.

AtomS3R includes neither a LiPo battery nor a LiPo charge circuit. Prototype on USB before choosing a compatible battery base. Keep magnetic haptic actuators away from the BMM150 or characterize their interference.
