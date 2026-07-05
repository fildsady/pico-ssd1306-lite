#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "font.h"

/* ── Hardware config — supplied by the PROJECT, not this repo ───────────────
 * Same pattern as FreeRTOSConfig.h: this header never defines the pins/
 * peripheral itself, it just #includes "oled_driver_config.h" and expects
 * whoever links this driver in to have that file somewhere on their
 * include path, defining:
 *
 *   OLED_I2C_PORT       - i2c0 or i2c1
 *   OLED_I2C_SDA        - GPIO number
 *   OLED_I2C_SCL        - GPIO number
 *   OLED_I2C_ADDR       - 7-bit I2C address (0x3C for most SSD1306 modules)
 *   OLED_I2C_FREQ       - I2C clock, Hz (400000 = fast mode)
 *   OLED_DMA_IRQ_INDEX  - 0 or 1: which shared DMA IRQ line (DMA_IRQ_0/
 *                         DMA_IRQ_1) this driver's completion handler uses.
 *                         Only matters if another driver in the same
 *                         project also hooks a DMA IRQ line and you need to
 *                         put them on different ones.
 *
 * See inc/oled_driver_config.example.h in this repo for a template — copy
 * it into your own project's include path as oled_driver_config.h and fill
 * in your board's actual pins. Without that file present, this driver
 * won't compile — that's intentional, there's no sane default pin wiring
 * to fall back to. */
#include "oled_driver_config.h"

#ifndef OLED_DMA_IRQ_INDEX
#define OLED_DMA_IRQ_INDEX 1
#endif

/* ── Display geometry ────────────────────────────────────────────────────── */
#define SSD1306_WIDTH    128
#define SSD1306_HEIGHT   64
#define SSD1306_PAGES    (SSD1306_HEIGHT / 8)
#define SSD1306_BUF_SIZE (SSD1306_WIDTH * SSD1306_PAGES)

/* ── API ─────────────────────────────────────────────────────────────────── */
/* Safe to call before the FreeRTOS scheduler has started (unlike
 * ssd1306_init() below) — pure blocking i2c_write_blocking() calls only, no
 * DMA channel claim, no semaphore, no ISR. Brings the panel up and blanks
 * its GDDRAM so it shows a clear screen instead of power-on garbage the
 * instant it's powered, without waiting for any FreeRTOS task to run.
 * ssd1306_init() (below) calls this itself and is safe to call afterward
 * regardless — it only sets up the DMA/semaphore path on top, it doesn't
 * redo the parts this already did. */
void ssd1306_init_early(void);

/* Pushes back_buf to the panel over a blocking I2C write — no DMA/
 * semaphore/ISR, safe before the scheduler starts. Draw a boot message with
 * the usual ssd1306_draw_*() calls into back_buf, then call this (instead
 * of ssd1306_update(), which needs ssd1306_init()'s DMA/scheduler) to
 * actually show it. Unlike ssd1306_update(), does not swap front_buf/
 * back_buf — nothing else is drawing concurrently at this point. */
void ssd1306_flush_blocking(void);

void ssd1306_init(void);
void ssd1306_clear(void);                             // clear back buffer
void ssd1306_update(void);                            // swap + start DMA
bool ssd1306_busy(void);                              // true while DMA runs
void ssd1306_draw_pixel(int x, int y, bool on);
void ssd1306_draw_char(int x, int y, char ch, const FontDef *font);
void ssd1306_draw_string(int x, int y, const char *s, const FontDef *font);
void ssd1306_draw_string_2x(int x, int y, const char *s, const FontDef *font);
void ssd1306_draw_string_7x10(int x, int y, const char *s);
void ssd1306_draw_string_8x16(int x, int y, const char *s);
void ssd1306_draw_string_8x8(int x, int y, const char *s);
void ssd1306_draw_string_12x16(int x, int y, const char *s);
void ssd1306_draw_string_12x24(int x, int y, const char *s);
void ssd1306_draw_string_16x32(int x, int y, const char *s);
void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
void ssd1306_invert_rect(int x, int y, int w, int h);

/* Shared low-level I2C+DMA sender — the ONE owner of OLED_I2C_PORT's DMA
 * channel/semaphore. ssd1306_update() uses this internally; any other
 * renderer sharing this same physical display (e.g. u8g2's HAL, see
 * u8g2_pico_hal.c) MUST send through this instead of standing up its own
 * DMA channel/semaphore — two independent drivers poking the same I2C
 * hardware caused a hang (untimed busy-wait in ssd1306_update() spinning
 * forever on a bus left non-idle by the other driver).
 * ssd1306_init() must be called first — it owns I2C/DMA setup.
 * Blocks the calling task (via semaphore, not busy-wait) until any
 * previous transfer completes, then kicks off this one and returns —
 * caller does not need to wait for completion. `data` must be <=
 * SSD1306_BUF_SIZE + 1 bytes (shared with the frame-buffer send). */
void ssd1306_i2c_send_dma(const uint8_t *data, size_t len);
