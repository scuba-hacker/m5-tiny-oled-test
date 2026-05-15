#ifndef TARGET_CONFIG_H
#define TARGET_CONFIG_H

// Flip this to 0 when building the same OLED demos for a plain ESP32-C3.
// The matching PlatformIO env should also be selected: release for M5, esp32c3 for C3.
#ifndef TARGET_M5STICKC_PLUS
#define TARGET_M5STICKC_PLUS 0
#endif

#define TARGET_ESP32_C3 (!TARGET_M5STICKC_PLUS)

#ifndef OLED_SDA
#if TARGET_M5STICKC_PLUS
#define OLED_SDA 32
#else
#define OLED_SDA 8
#endif
#endif

#ifndef OLED_SCK
#if TARGET_M5STICKC_PLUS
#define OLED_SCK 33
#else
#define OLED_SCK 9
#endif
#endif

#ifndef I2C_ADDRESS
#define I2C_ADDRESS 0x78
#endif

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#endif
