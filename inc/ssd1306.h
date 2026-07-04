#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "font.h"

/* ── Hardware pins — change to match wiring ──────────────────────────────── */
/* On GP2/3 (i2c1) rather than GP4/5 (i2c0) — freed GP4/5 up for UART1, the
 * pin pair a second RS485/Modbus/DMX bus would need. */
#define OLED_I2C_PORT   i2c1
#define OLED_I2C_SDA    2
#define OLED_I2C_SCL    3
#define OLED_I2C_ADDR   0x3C
#define OLED_I2C_FREQ   400000

/* ── Display geometry ────────────────────────────────────────────────────── */
#define SSD1306_WIDTH    128
#define SSD1306_HEIGHT   64
#define SSD1306_PAGES    (SSD1306_HEIGHT / 8)
#define SSD1306_BUF_SIZE (SSD1306_WIDTH * SSD1306_PAGES)

/* ── API ─────────────────────────────────────────────────────────────────── */
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
void ssd1306_fill_rect(int x, int y, int w, int h, bool on);
void ssd1306_invert_rect(int x, int y, int w, int h);
