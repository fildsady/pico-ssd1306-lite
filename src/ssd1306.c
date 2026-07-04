#include "ssd1306.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

/* ── Double buffers ──────────────────────────────────────────────────────── */
static uint8_t  buf_a[SSD1306_BUF_SIZE];
static uint8_t  buf_b[SSD1306_BUF_SIZE];
static uint8_t *front_buf = buf_a;   /* DMA reads this */
static uint8_t *back_buf  = buf_b;   /* task draws here */

/* ── DMA state ───────────────────────────────────────────────────────────── */
/*
 * Pico I2C requires 32-bit IC_DATA_CMD writes, not raw byte DMA.
 * Each word: bits[7:0]=data, bit[9]=STOP (last byte only).
 * Buffer: 1 ctrl byte (0x40) + 1024 pixel bytes = 1025 words = 4100 bytes.
 */
static uint32_t         s_cmd_buf[1 + SSD1306_BUF_SIZE];
static int              s_dma_ch  = -1;
static SemaphoreHandle_t s_done   = NULL;
static volatile bool     s_busy   = false;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void send_cmds(const uint8_t *buf, size_t len)
{
    i2c_write_blocking(OLED_I2C_PORT, OLED_I2C_ADDR, buf, len, false);
}

/* Build IC_DATA_CMD word array from pixel buffer.
 * First word = ctrl byte 0x40 (no STOP).
 * Last word  = last pixel | (1<<9) STOP. */
static void build_cmd_buf(const uint8_t *pixels)
{
    s_cmd_buf[0] = 0x40u;
    for (int i = 0; i < SSD1306_BUF_SIZE - 1; i++)
        s_cmd_buf[1 + i] = pixels[i];
    s_cmd_buf[SSD1306_BUF_SIZE] = (uint32_t)pixels[SSD1306_BUF_SIZE - 1] | (1u << 9);
}

/* ── DMA completion ISR ──────────────────────────────────────────────────── */

static void dma_irq_handler(void)
{
    if (!dma_channel_get_irq1_status(s_dma_ch)) return;
    dma_channel_acknowledge_irq1(s_dma_ch);

    s_busy = false;

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void ssd1306_init(void)
{
    /* I2C hardware */
    i2c_init(OLED_I2C_PORT, OLED_I2C_FREQ);
    gpio_set_function(OLED_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(OLED_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_I2C_SDA);
    gpio_pull_up(OLED_I2C_SCL);
    sleep_ms(100);

    /* DMA channel — configured once, reused every frame */
    s_dma_ch = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_dreq(&dc, i2c_get_dreq(OLED_I2C_PORT, true));
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    dma_channel_set_config(s_dma_ch, &dc, false);
    dma_channel_set_write_addr(s_dma_ch, &i2c_get_hw(OLED_I2C_PORT)->data_cmd, false);
    dma_channel_set_irq1_enabled(s_dma_ch, true);

    irq_add_shared_handler(DMA_IRQ_1, dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_1, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);

    /* Semaphore — pre-armed so first ssd1306_update() doesn't block */
    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done);

    /* Display init sequence */
    static const uint8_t init_seq[] = {
        0x00,
        0xAE,         /* display off */
        0xD5, 0x80,   /* clock divide */
        0xA8, 0x3F,   /* multiplex 64 */
        0xD3, 0x00,   /* offset 0 */
        0x40,         /* start line 0 */
        0x8D, 0x14,   /* charge pump on */
        0x20, 0x00,   /* horizontal addressing */
        0xA1,         /* seg remap */
        0xC8,         /* COM reversed */
        0xDA, 0x12,   /* COM pins */
        0x81, 0xCF,   /* contrast */
        0xD9, 0xF1,   /* pre-charge */
        0xDB, 0x40,   /* VCOMH */
        0xA4,         /* output from RAM */
        0xA6,         /* normal display */
        0xAF,         /* display on */
    };
    send_cmds(init_seq, sizeof(init_seq));

    memset(buf_a, 0, SSD1306_BUF_SIZE);
    memset(buf_b, 0, SSD1306_BUF_SIZE);
    ssd1306_update();
}

void ssd1306_clear(void)
{
    memset(back_buf, 0, SSD1306_BUF_SIZE);
}

bool ssd1306_busy(void) { return s_busy; }

/* ── Double-buffer DMA update ────────────────────────────────────────────
 *
 * Frame timeline @ 30fps (period = 33ms, DMA ≈ 23ms):
 *
 *   t=0ms   ssd1306_update() called
 *             semaphore take      (≈0ms — DMA finished 10ms ago)
 *             I2C idle check      (≈0ms — already idle)
 *             swap buffers
 *             addr-window cmd     (~0.16ms blocking)
 *             build cmd_buf       (~0.2ms memcpy-style loop)
 *             start DMA           returns immediately
 *   t=0.4ms  returns → task draws next frame into back_buf freely
 *   t=23ms   DMA ISR: give semaphore
 *   t=33ms   vTaskDelay expires → repeat
 *
 * During the 23ms DMA phase, all other tasks run normally.
 * ─────────────────────────────────────────────────────────────────────── */
void ssd1306_update(void)
{
    /* Wait for previous frame's DMA to finish */
    xSemaphoreTake(s_done, pdMS_TO_TICKS(100));

    /* Ensure I2C master is fully idle before touching it.
     * At 30fps this is instant — FIFO drains in <1ms, we arrive 10ms later. */
    while (i2c_get_hw(OLED_I2C_PORT)->status & I2C_IC_STATUS_MST_ACTIVITY_BITS)
        tight_loop_contents();

    /* Swap: front_buf ← newly drawn frame (DMA reads)
     *       back_buf  ← old front (task draws next frame here) */
    uint8_t *tmp = front_buf;
    front_buf    = back_buf;
    back_buf     = tmp;

    /* Address window — short blocking transaction (~0.16ms) */
    static const uint8_t addr_win[] = {
        0x00, 0x21, 0x00, 0x7F,
              0x22, 0x00, 0x07,
    };
    send_cmds(addr_win, sizeof(addr_win));

    /* Build IC_DATA_CMD word buffer from front_buf */
    build_cmd_buf(front_buf);

    /* Set up I2C for new transaction (same address, just re-arm) */
    i2c_get_hw(OLED_I2C_PORT)->enable = 0;
    i2c_get_hw(OLED_I2C_PORT)->tar    = OLED_I2C_ADDR;
    i2c_get_hw(OLED_I2C_PORT)->enable = 1;

    /* Start DMA — I2C controller issues START automatically when FIFO fills */
    s_busy = true;
    dma_channel_set_read_addr (s_dma_ch, s_cmd_buf, false);
    dma_channel_set_trans_count(s_dma_ch, 1 + SSD1306_BUF_SIZE, true);  /* true = trigger */
    /* returns immediately — task can draw back_buf while DMA runs */
}

/* ── Draw primitives ─────────────────────────────────────────────────────── */

void ssd1306_draw_pixel(int x, int y, bool on)
{
    if ((unsigned)x >= SSD1306_WIDTH || (unsigned)y >= SSD1306_HEIGHT) return;
    int idx = x + (y / 8) * SSD1306_WIDTH;
    if (on) back_buf[idx] |=  (1u << (y % 8));
    else    back_buf[idx] &= ~(1u << (y % 8));
}

void ssd1306_draw_char(int x, int y, char ch, const FontDef *font)
{
    if (ch < 32 || ch > 126) return;
    uint16_t ci = (uint16_t)(ch - 32) * (font->width - 1);
    for (uint8_t col = 0; col < font->width - 1; col++) {
        uint8_t col_data = font->data[ci + col];
        for (uint8_t row = 0; row < font->height; row++) {
            if ((col_data >> row) & 1)
                ssd1306_draw_pixel(x + col, y + row, true);
        }
    }
}

void ssd1306_draw_string(int x, int y, const char *s, const FontDef *font)
{
    for (; *s; s++, x += font->width)
        ssd1306_draw_char(x, y, *s, font);
}

void ssd1306_draw_string_2x(int x, int y, const char *s, const FontDef *font)
{
    for (; *s; s++, x += font->width * 2) {
        if (*s < 32 || *s > 126) continue;
        uint16_t ci = (uint16_t)(*s - 32) * (font->width - 1);
        for (uint8_t col = 0; col < font->width - 1; col++) {
            uint8_t col_data = font->data[ci + col];
            for (uint8_t row = 0; row < font->height; row++) {
                bool on = (col_data >> row) & 1;
                ssd1306_draw_pixel(x + col*2,     y + row*2,     on);
                ssd1306_draw_pixel(x + col*2 + 1, y + row*2,     on);
                ssd1306_draw_pixel(x + col*2,     y + row*2 + 1, on);
                ssd1306_draw_pixel(x + col*2 + 1, y + row*2 + 1, on);
            }
        }
    }
}

void ssd1306_draw_string_7x10(int x, int y, const char *s)
{
    for (; *s; s++, x += 7) {
        if (*s < 32 || *s > 126) continue;
        uint16_t ci = (uint16_t)(*s - 32) * 10;
        for (uint8_t row = 0; row < 10; row++) {
            uint16_t row_data = Font7x10_Data[ci + row];
            for (uint8_t col = 0; col < 7; col++) {
                if (row_data & (0x8000u >> col))
                    ssd1306_draw_pixel(x + col, y + row, true);
            }
        }
    }
}

/* 8x16, from Font8x16_Data — 16 bytes/glyph, 8 columns x 2 vertical pages
 * (top page bytes 0-7, bottom page bytes 8-15). The only font here that
 * divides 128px evenly into 16 columns, matching a 16x2 LCD's char grid. */
void ssd1306_draw_string_8x16(int x, int y, const char *s)
{
    for (; *s; s++, x += 8) {
        if (*s < 32 || *s > 126) continue;
        uint16_t ci = (uint16_t)(*s - 32) * 16;
        for (uint8_t col = 0; col < 8; col++) {
            uint8_t top = Font8x16_Data[ci + col];
            uint8_t bot = Font8x16_Data[ci + 8 + col];
            for (uint8_t row = 0; row < 8; row++) {
                if ((top >> row) & 1) ssd1306_draw_pixel(x + col, y + row, true);
                if ((bot >> row) & 1) ssd1306_draw_pixel(x + col, y + row + 8, true);
            }
        }
    }
}

/* Row-major bit-packed renderer — Font8x8/12x16/12x24/16x32_Data (see
 * font.h) store ceil(width/8) bytes per row, MSB-first, top-to-bottom,
 * unlike the column-major fonts above. One shared implementation
 * parameterized by width/height/bytes-per-row; ssd1306_draw_string_8x8()/
 * _12x16()/_12x24()/_16x32() below just pass in their own font's data
 * pointer and dimensions. */
static void draw_string_rowmajor(int x, int y, const char *s, const uint8_t *data,
                                  uint8_t width, uint8_t height, uint8_t bytes_per_row)
{
    uint16_t glyph_size = (uint16_t)bytes_per_row * height;
    for (; *s; s++, x += width) {
        if (*s < 32 || *s > 126) continue;
        const uint8_t *glyph = data + (uint16_t)(*s - 32) * glyph_size;
        for (uint8_t row = 0; row < height; row++) {
            const uint8_t *row_bytes = glyph + (uint16_t)row * bytes_per_row;
            for (uint8_t col = 0; col < width; col++) {
                uint8_t byte = row_bytes[col / 8];
                if ((byte >> (7 - (col % 8))) & 1) ssd1306_draw_pixel(x + col, y + row, true);
            }
        }
    }
}

void ssd1306_draw_string_8x8(int x, int y, const char *s)
{
    draw_string_rowmajor(x, y, s, Font8x8_Data, 8, 8, 1);
}

void ssd1306_draw_string_12x16(int x, int y, const char *s)
{
    draw_string_rowmajor(x, y, s, Font12x16_Data, 12, 16, 2);
}

void ssd1306_draw_string_12x24(int x, int y, const char *s)
{
    draw_string_rowmajor(x, y, s, Font12x24_Data, 12, 24, 2);
}

void ssd1306_draw_string_16x32(int x, int y, const char *s)
{
    draw_string_rowmajor(x, y, s, Font16x32_Data, 16, 32, 2);
}

void ssd1306_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            ssd1306_draw_pixel(px, py, on);
}

void ssd1306_invert_rect(int x, int y, int w, int h)
{
    for (int py = y; py < y + h && py < SSD1306_HEIGHT; py++) {
        int page = py / 8, bit = py % 8;
        for (int px = x; px < x + w && px < SSD1306_WIDTH; px++)
            back_buf[page * SSD1306_WIDTH + px] ^= (1u << bit);
    }
}
