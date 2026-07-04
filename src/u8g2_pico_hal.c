/* Pico SDK HAL for u8g2 — see u8g2_pico_hal.h. Blocking hardware_i2c calls
 * only (no DMA, no FreeRTOS dependency) — u8g2 always sends a whole page/
 * buffer in one START/SEND.../END sequence, so buffering that into a single
 * i2c_write_blocking() call per transfer is enough; there's no need for the
 * double-buffer/DMA machinery ssd1306.c uses for its own (separate)
 * rendering path. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "u8g2_pico_hal.h"
#include "ssd1306.h" /* OLED_I2C_PORT/SDA/SCL/FREQ — same pins/bus as ssd1306.c */

/* u8g2 never sends more than one SSD1306 command/data block at a time
 * (a handful of init command bytes, or one 1+1024-byte "0x40 + framebuffer"
 * data block) — 1100 gives headroom without dynamic allocation. */
#define U8G2_PICO_HAL_I2C_BUF_SIZE 1100

static uint8_t s_i2c_buf[U8G2_PICO_HAL_I2C_BUF_SIZE];
static size_t  s_i2c_buf_len;
static bool    s_i2c_initialized = false;

uint8_t u8g2_pico_hal_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_BYTE_INIT:
        if (!s_i2c_initialized) {
            i2c_init(OLED_I2C_PORT, OLED_I2C_FREQ);
            gpio_set_function(OLED_I2C_SDA, GPIO_FUNC_I2C);
            gpio_set_function(OLED_I2C_SCL, GPIO_FUNC_I2C);
            gpio_pull_up(OLED_I2C_SDA);
            gpio_pull_up(OLED_I2C_SCL);
            s_i2c_initialized = true;
        }
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

    case U8X8_MSG_BYTE_END_TRANSFER: {
        uint8_t addr7 = u8x8_GetI2CAddress(u8x8) >> 1;
        i2c_write_blocking(OLED_I2C_PORT, addr7, s_i2c_buf, s_i2c_buf_len, false);
        break;
    }

    case U8X8_MSG_BYTE_SET_DC:
        break; /* I2C mode has no D/C pin — only relevant for 4-wire SPI displays */

    default:
        return 0; /* unhandled message */
    }
    return 1;
}

uint8_t u8g2_pico_hal_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;
    switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        break; /* no reset/CS/DC pins wired for this I2C-only OLED module */
    case U8X8_MSG_DELAY_MILLI:
        sleep_ms(arg_int);
        break;
    case U8X8_MSG_DELAY_10MICRO:
        sleep_us(arg_int * 10);
        break;
    case U8X8_MSG_DELAY_100NANO:
        sleep_us(1); /* rounds up — RP2350 has no sub-microsecond sleep primitive */
        break;
    default:
        break; /* GPIO pin messages (reset/CS/DC) — no-op, none of those are wired */
    }
    return 1;
}
