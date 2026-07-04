/* Copy this file into your own project's include path as
 * "oled_driver_config.h" and fill in your board's actual wiring — same
 * pattern as FreeRTOSConfig.h. ssd1306.h #includes "oled_driver_config.h"
 * without knowing where it lives; your project supplies it. */
#pragma once

/* I2C peripheral instance (i2c0 or i2c1) and pins wired to the SSD1306. */
#define OLED_I2C_PORT   i2c1
#define OLED_I2C_SDA    2
#define OLED_I2C_SCL    3

/* 7-bit I2C address — 0x3C is correct for the overwhelming majority of
 * SSD1306 modules (0x3D on some, check your module's datasheet/silkscreen
 * if 0x3C doesn't respond). */
#define OLED_I2C_ADDR   0x3C

/* I2C clock, Hz. 400000 = fast mode, supported by essentially every
 * SSD1306 module; drop to 100000 (standard mode) only if you have signal
 * integrity problems on long/unshielded wiring. */
#define OLED_I2C_FREQ   400000

/* Which shared DMA IRQ line (0 = DMA_IRQ_0, 1 = DMA_IRQ_1) this driver's
 * completion handler hooks. Only matters if another driver in your
 * project also hooks a DMA IRQ line and the two need separating — pick
 * whichever index isn't already in use. Defaults to 1 if omitted. */
#define OLED_DMA_IRQ_INDEX 1
