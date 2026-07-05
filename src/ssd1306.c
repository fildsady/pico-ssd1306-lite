#include "ssd1306.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

/* Which shared DMA IRQ line (DMA_IRQ_0/DMA_IRQ_1) to hook — selected via
 * OLED_DMA_IRQ_INDEX in the project's oled_driver_config.h (see ssd1306.h).
 * Pico SDK's irqN-suffixed functions aren't parameterizable by index, so
 * this picks the right set of them at compile time instead. */
#if OLED_DMA_IRQ_INDEX == 0
#define OLED_DMA_IRQ                     DMA_IRQ_0
#define oled_dma_channel_set_irq_enabled dma_channel_set_irq0_enabled
#define oled_dma_channel_get_irq_status  dma_channel_get_irq0_status
#define oled_dma_channel_acknowledge_irq dma_channel_acknowledge_irq0
#else
#define OLED_DMA_IRQ                     DMA_IRQ_1
#define oled_dma_channel_set_irq_enabled dma_channel_set_irq1_enabled
#define oled_dma_channel_get_irq_status  dma_channel_get_irq1_status
#define oled_dma_channel_acknowledge_irq dma_channel_acknowledge_irq1
#endif

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

/* ── DMA completion ISR ──────────────────────────────────────────────────── */

static void dma_irq_handler(void)
{
    if (!oled_dma_channel_get_irq_status(s_dma_ch)) return;
    oled_dma_channel_acknowledge_irq(s_dma_ch);

    s_busy = false;

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Display init sequence — file-scope (not local to ssd1306_init_early())
 * since nothing else needs it, but kept as one named array either way). */
static const uint8_t s_init_seq[] = {
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

void ssd1306_init_early(void)
{
    /* Idempotent, same reasoning as ssd1306_init()'s own guard below —
     * separate flag since this can run standalone (before the scheduler
     * starts) and/or be followed by the full ssd1306_init() later. */
    static bool s_early_done = false;
    if (s_early_done) return;
    s_early_done = true;

    i2c_init(OLED_I2C_PORT, OLED_I2C_FREQ);
    gpio_set_function(OLED_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(OLED_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_I2C_SDA);
    gpio_pull_up(OLED_I2C_SCL);
    sleep_ms(50);

    send_cmds(s_init_seq, sizeof(s_init_seq));

    /* Blank both buffers up front, then push once — draw calls between here
     * and the real ssd1306_init()/ssd1306_update() (e.g. a boot message
     * drawn from main(), see ssd1306_flush_blocking()) go into back_buf,
     * same as always. */
    memset(buf_a, 0, SSD1306_BUF_SIZE);
    memset(buf_b, 0, SSD1306_BUF_SIZE);
    ssd1306_flush_blocking();
}

void ssd1306_flush_blocking(void)
{
    /* Pushes back_buf directly over a blocking I2C write — no DMA, no
     * semaphore, no ISR, unlike ssd1306_i2c_send_dma()/ssd1306_update()
     * (which need ssd1306_init()'s DMA channel + FreeRTOS scheduler
     * running). For drawing a boot message before the scheduler starts —
     * draw into back_buf with the usual ssd1306_draw_*() calls, then call
     * this instead of ssd1306_update() to actually show it. Slower (blocks
     * the caller for the whole ~23ms transfer) but that's irrelevant pre-
     * scheduler since nothing else is running yet anyway. Does NOT swap
     * front_buf/back_buf like ssd1306_update() does — there's no "other"
     * task drawing the next frame concurrently at this point, so there's
     * nothing to swap for. */
    static const uint8_t addr_win[] = { 0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07 };
    send_cmds(addr_win, sizeof(addr_win));
    static uint8_t frame[1 + SSD1306_BUF_SIZE];
    frame[0] = 0x40;
    memcpy(frame + 1, back_buf, SSD1306_BUF_SIZE);
    i2c_write_blocking(OLED_I2C_PORT, OLED_I2C_ADDR, frame, sizeof(frame), false);
}

void ssd1306_init(void)
{
    /* Idempotent — safe to call more than once (e.g. u8g2_pico_hal_init()
     * calls this unconditionally so it works standalone or alongside code
     * that already called ssd1306_init() itself). Without this guard, a
     * second call would re-claim a DMA channel and recreate the semaphore
     * out from under any in-flight transfer. */
    static bool s_initialized = false;
    if (s_initialized) return;
    s_initialized = true;

    /* Covers I2C/GPIO bring-up + the init command sequence — already done
     * if the caller (or something earlier, e.g. main()) called
     * ssd1306_init_early() first; harmless/no-op if so, otherwise does it
     * now. Either way, everything from here down (DMA/semaphore/ISR) needs
     * the FreeRTOS scheduler already running. */
    ssd1306_init_early();

    /* DMA channel — configured once, reused every frame */
    s_dma_ch = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_dreq(&dc, i2c_get_dreq(OLED_I2C_PORT, true));
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    dma_channel_set_config(s_dma_ch, &dc, false);
    dma_channel_set_write_addr(s_dma_ch, &i2c_get_hw(OLED_I2C_PORT)->data_cmd, false);
    oled_dma_channel_set_irq_enabled(s_dma_ch, true);

    irq_add_shared_handler(OLED_DMA_IRQ, dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(OLED_DMA_IRQ, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    irq_set_enabled(OLED_DMA_IRQ, true);

    /* Semaphore — pre-armed so first ssd1306_update() doesn't block */
    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done);

    /* Deliberately NOT memset-ing buf_a/buf_b to 0 here — used to, but that
     * silently wiped out a boot message drawn into back_buf before this
     * ran (main()'s ssd1306_draw_string_8x16()+ssd1306_flush_blocking()),
     * replacing it with a blank frame via this ssd1306_update() call before
     * the caller (task_lcd) ever got a chance to actually show it. Buffers
     * already start zeroed at compile time (static arrays) and
     * ssd1306_init_early() blanks them again if it runs standalone without
     * anything drawing over it — nothing here needs a fresh blank. */
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
/* Shared sender — see doc comment in ssd1306.h. This is the ONLY code path
 * that touches s_dma_ch/s_done/the I2C enable/tar registers for a DMA
 * transfer; ssd1306_update() and any other renderer (u8g2's HAL) funnel
 * through here so there's exactly one owner of the hardware. */
void ssd1306_i2c_send_dma(const uint8_t *data, size_t len)
{
    if (len == 0 || len > 1 + SSD1306_BUF_SIZE) return;

    /* Wait for previous transfer's DMA to finish */
    xSemaphoreTake(s_done, pdMS_TO_TICKS(100));

    /* Ensure I2C master is fully idle before touching it. */
    while (i2c_get_hw(OLED_I2C_PORT)->status & I2C_IC_STATUS_MST_ACTIVITY_BITS)
        tight_loop_contents();

    for (size_t i = 0; i < len; i++)
        s_cmd_buf[i] = data[i];
    s_cmd_buf[len - 1] |= (1u << 9);   /* STOP on last byte */

    /* Set up I2C for new transaction (same address, just re-arm) */
    i2c_get_hw(OLED_I2C_PORT)->enable = 0;
    i2c_get_hw(OLED_I2C_PORT)->tar    = OLED_I2C_ADDR;
    i2c_get_hw(OLED_I2C_PORT)->enable = 1;

    /* Start DMA — I2C controller issues START automatically when FIFO fills */
    s_busy = true;
    dma_channel_set_read_addr (s_dma_ch, s_cmd_buf, false);
    dma_channel_set_trans_count(s_dma_ch, len, true);  /* true = trigger */
    /* returns immediately — caller can continue while DMA runs */
}

void ssd1306_update(void)
{
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

    /* Build ctrl-byte + pixel-byte array, hand off to the shared sender */
    static uint8_t frame_bytes[1 + SSD1306_BUF_SIZE];
    frame_bytes[0] = 0x40u;
    memcpy(frame_bytes + 1, front_buf, SSD1306_BUF_SIZE);
    ssd1306_i2c_send_dma(frame_bytes, sizeof(frame_bytes));
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
