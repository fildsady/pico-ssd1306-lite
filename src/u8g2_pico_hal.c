/* Pico SDK + FreeRTOS HAL for u8g2 — see u8g2_pico_hal.h. This does NOT own
 * any I2C/DMA hardware itself — it funnels every transfer through
 * ssd1306_i2c_send_dma() (see ssd1306.c/.h), the single shared DMA
 * channel/semaphore that already owns OLED_I2C_PORT. u8g2 and the ssd1306.c
 * rendering path share one physical display and one physical I2C
 * peripheral; having two independent drivers each with their own DMA
 * channel/semaphore poking the same hardware caused a real hang (an
 * untimed busy-wait in ssd1306_update() spinning forever on a bus left
 * non-idle by the other driver). Routing both through one sender fixes
 * that at the root instead of avoiding the combination.
 * ssd1306_init() must run before this HAL is used — it owns I2C/DMA setup. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u8g2_pico_hal.h"
#include "ssd1306.h" /* OLED_I2C_PORT + ssd1306_i2c_send_dma() — shared hardware owner */

/* u8g2 never sends more than one SSD1306 command/data block at a time (a
 * handful of init command bytes, or one 1+1024-byte "0x40 + framebuffer"
 * data block) — matches ssd1306_i2c_send_dma()'s own 1+SSD1306_BUF_SIZE cap. */
#define U8G2_PICO_HAL_I2C_BUF_SIZE (1 + SSD1306_BUF_SIZE)

static uint8_t s_i2c_buf[U8G2_PICO_HAL_I2C_BUF_SIZE];
static size_t  s_i2c_buf_len;

uint8_t u8g2_pico_hal_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    switch (msg) {
    case U8X8_MSG_BYTE_INIT:
        /* I2C/DMA already brought up by ssd1306_init() — nothing to do. */
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        s_i2c_buf_len = 0;
        break;

    case U8X8_MSG_BYTE_SEND:
        if (s_i2c_buf_len + arg_int <= sizeof(s_i2c_buf)) {
            memcpy(s_i2c_buf + s_i2c_buf_len, arg_ptr, arg_int);
            s_i2c_buf_len += arg_int;
        }
        break;

    case U8X8_MSG_BYTE_END_TRANSFER:
        if (s_i2c_buf_len == 0) break;
        /* Blocks (yielding the CPU) only if a previous transfer — from
         * either this HAL or ssd1306_update() — hasn't finished yet. */
        ssd1306_i2c_send_dma(s_i2c_buf, s_i2c_buf_len);
        break;

    case U8X8_MSG_BYTE_SET_DC:
        break; /* I2C mode has no D/C pin — only relevant for 4-wire SPI displays */

    default:
        return 0; /* unhandled message */
    }
    return 1;
}

void u8g2_pico_hal_init(u8g2_t *u8g2)
{
    ssd1306_init(); /* idempotent — brings up the shared I2C/DMA driver if not already up */
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2, U8G2_R0,
        u8g2_pico_hal_byte_cb, u8g2_pico_hal_gpio_and_delay_cb);
    u8g2_InitDisplay(u8g2);
    u8g2_SetPowerSave(u8g2, 0);
}

uint8_t u8g2_pico_hal_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;
    switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        break; /* no reset/CS/DC pins wired for this I2C-only OLED module */
    case U8X8_MSG_DELAY_MILLI:
        /* u8x8_d_helper_display_init() (u8x8_display.c) sandwiches its
         * reset-pin toggle between 3 of these calls — reset_pulse_width_ms
         * and post_reset_wait_ms are both 100ms for this display variant
         * (u8x8_d_ssd1306_128x64_noname.c), so ~300ms total. Since this
         * module has no reset pin wired at all (the GPIO toggle above is
         * already a no-op), that whole 300ms was dead time waiting for a
         * pin transition that never physically happens — measured as most
         * of task_lcd's ~0.5s gap between the pre-scheduler "Booting..."
         * message and its own first real frame. Skipping it entirely (not
         * just shortening) is safe specifically because there's no reset
         * pin to settle; a display module that DID have one wired would
         * need this real. */
        break;
    case U8X8_MSG_DELAY_10MICRO:
        sleep_us(arg_int * 10); /* sub-tick delays too short to yield usefully — busy-wait is fine here */
        break;
    case U8X8_MSG_DELAY_100NANO:
        sleep_us(1); /* rounds up — RP2350 has no sub-microsecond sleep primitive */
        break;
    default:
        break; /* GPIO pin messages (reset/CS/DC) — no-op, none of those are wired */
    }
    return 1;
}
