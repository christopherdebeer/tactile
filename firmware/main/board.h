#pragma once
/* AtomS3R: https://docs.m5stack.com/en/core/AtomS3R
 * INTERNAL SYS I2C: SCL=GPIO0, SDA=GPIO45 (BMI270 @ 0x68 + LP5562)
 * SCREEN BUTTON: GPIO41 (active low)
 * EXTERNAL GROVE: G1 white, G2 yellow, red 5V (NOT 3.3V)
 * LCD hardware revisions vary: intentionally not initialized in P0.
 */
#define TACTILE_SYS_SCL 0
#define TACTILE_SYS_SDA 45
#define TACTILE_BUTTON 41
#define TACTILE_BMI270_ADDR 0x68
#define TACTILE_BMI270_CHIP_ID 0x24
