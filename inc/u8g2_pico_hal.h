#ifndef U8G2_PICO_HAL_H
#define U8G2_PICO_HAL_H
#include "u8g2.h"

/* Pico SDK + FreeRTOS HAL for u8g2 — non-blocking DMA-to-I2C (same
 * technique/hardware trick as ssd1306.c's own rendering path: END_TRANSFER
 * starts the DMA and returns immediately; a semaphore given from the
 * DMA-complete ISR is what the *next* transfer waits on if it starts before
 * the previous one finished, so a blocked task actually yields the CPU
 * instead of busy-polling), using the same OLED_I2C_PORT/SDA/SCL/FREQ/ADDR
 * pins as ssd1306.c (see ssd1306.h). This is a *separate* rendering path
 * from ssd1306.c/font.h above — u8g2 has its own SSD1306 driver +
 * framebuffer, they don't share code, just the same physical I2C bus/pins.
 * Pick one or the other per project, not both at once on the same display.
 *
 * Requires FreeRTOS (FreeRTOS.h/task.h/semphr.h) — same dependency as
 * ssd1306.c, not intended for bare-metal use.
 *
 * Usage (from within a FreeRTOS task):
 *   u8g2_t u8g2;
 *   u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
 *       u8g2_pico_hal_byte_cb, u8g2_pico_hal_gpio_and_delay_cb);
 *   u8g2_InitDisplay(&u8g2);
 *   u8g2_SetPowerSave(&u8g2, 0);
 *   u8g2_ClearBuffer(&u8g2);
 *   u8g2_DrawStr(&u8g2, 0, 20, "Hello");
 *   u8g2_SendBuffer(&u8g2);
 */
uint8_t u8g2_pico_hal_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8g2_pico_hal_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#endif
