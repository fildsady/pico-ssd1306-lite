#ifndef U8G2_PICO_HAL_H
#define U8G2_PICO_HAL_H
#include "u8g2.h"

/* Pico SDK + FreeRTOS HAL for u8g2 — the primary/recommended rendering path
 * in this repo (see README). Non-blocking DMA-to-I2C: does NOT own any
 * hardware itself, every transfer is funneled through ssd1306_i2c_send_dma()
 * (ssd1306.c/.h), the single shared DMA channel/semaphore for
 * OLED_I2C_PORT. u8g2 has its own SSD1306 driver + framebuffer (doesn't
 * share rendering code with ssd1306.c/font.h's own minimal fonts), but both
 * paths go through the same low-level sender, so they're safe to call from
 * the same task on the same physical display — see ssd1306.h for why that
 * matters (two independent DMA/semaphore owners on one I2C peripheral
 * caused a real hang).
 *
 * Requires FreeRTOS (FreeRTOS.h/task.h) — same dependency as ssd1306.c, not
 * intended for bare-metal use.
 *
 * Usage (from within a FreeRTOS task):
 *   u8g2_t u8g2;
 *   u8g2_pico_hal_init(&u8g2);
 *   u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
 *   u8g2_ClearBuffer(&u8g2);
 *   u8g2_DrawStr(&u8g2, 0, 20, "Hello");
 *   u8g2_SendBuffer(&u8g2);
 */
uint8_t u8g2_pico_hal_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8g2_pico_hal_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

/* One-call setup: brings up the shared I2C/DMA driver (ssd1306_init()) if
 * it hasn't run yet, then does u8g2's own Setup/InitDisplay/SetPowerSave.
 * Safe to call even if ssd1306_init() was already called elsewhere (e.g. a
 * project also using ssd1306.c's own font.h renderer on other screens). */
void u8g2_pico_hal_init(u8g2_t *u8g2);

#endif
