/* Pico SDK + FreeRTOS HAL for u8g2 — see u8g2_pico_hal.h. u8g2 only cares
 * that the byte callback hands its data off to the wire and returns — it
 * never waits for the physical transfer to finish, so this uses the same
 * non-blocking DMA-to-I2C technique ssd1306.c uses for its own (separate)
 * rendering path: END_TRANSFER starts the DMA and returns immediately,
 * letting the RP2350's I2C hardware push bytes out in the background while
 * the caller (and every other FreeRTOS task) goes on running. A semaphore,
 * given from the DMA-complete ISR, is what the *next* transfer waits on if
 * it happens to start before the previous one finished — same pattern as
 * ssd1306.c's s_done, so a task blocked here actually yields the CPU to
 * other tasks instead of busy-polling it away. */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "u8g2_pico_hal.h"
#include "ssd1306.h" /* OLED_I2C_PORT/SDA/SCL/FREQ — same pins/bus as ssd1306.c */

/* u8g2 never sends more than one SSD1306 command/data block at a time (a
 * handful of init command bytes, or one 1+1024-byte "0x40 + framebuffer"
 * data block) — 1100 gives headroom without dynamic allocation. */
#define U8G2_PICO_HAL_I2C_BUF_SIZE 1100

static uint8_t  s_i2c_buf[U8G2_PICO_HAL_I2C_BUF_SIZE];
static size_t   s_i2c_buf_len;
static bool     s_i2c_initialized = false;

/* Same "Pico I2C DMA needs 32-bit IC_DATA_CMD words, STOP bit in bit 9 of
 * the last word" trick as ssd1306.c's s_cmd_buf/build_cmd_buf — see that
 * file's comment for the hardware reasoning. */
static uint32_t          s_cmd_buf[U8G2_PICO_HAL_I2C_BUF_SIZE];
static int                s_dma_ch = -1;
static SemaphoreHandle_t  s_done   = NULL;

static void u8g2_pico_hal_dma_irq_handler(void)
{
    if (!dma_channel_get_irq1_status(s_dma_ch)) return;
    dma_channel_acknowledge_irq1(s_dma_ch);

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    portYIELD_FROM_ISR(woken);
}

static void u8g2_pico_hal_i2c_dma_init(void)
{
    if (s_dma_ch >= 0) return;
    s_dma_ch = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_dreq(&dc, i2c_get_dreq(OLED_I2C_PORT, true));
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    dma_channel_set_config(s_dma_ch, &dc, false);
    dma_channel_set_write_addr(s_dma_ch, &i2c_get_hw(OLED_I2C_PORT)->data_cmd, false);
    dma_channel_set_irq1_enabled(s_dma_ch, true);

    irq_add_shared_handler(DMA_IRQ_1, u8g2_pico_hal_dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_priority(DMA_IRQ_1, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    irq_set_enabled(DMA_IRQ_1, true);

    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done); /* pre-armed so the first transfer doesn't block */
}

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
            u8g2_pico_hal_i2c_dma_init();
            s_i2c_initialized = true;
        }
        break;

    case U8X8_MSG_BYTE_START_TRANSFER:
        /* Blocks (yielding the CPU to other tasks) only if the previous
         * DMA transfer hasn't finished yet — near-instant in practice,
         * since there's always some drawing/logic time between two
         * u8g2_SendBuffer() calls. Must happen before touching
         * s_i2c_buf/s_cmd_buf again, since the DMA channel reads directly
         * from s_cmd_buf until it's done. */
        xSemaphoreTake(s_done, pdMS_TO_TICKS(100));
        while (i2c_get_hw(OLED_I2C_PORT)->status & I2C_IC_STATUS_MST_ACTIVITY_BITS)
            tight_loop_contents(); /* ensure the I2C master is fully idle before reusing it — same as ssd1306.c */
        s_i2c_buf_len = 0;
        break;

    case U8X8_MSG_BYTE_SEND:
        if (s_i2c_buf_len + arg_int <= sizeof(s_i2c_buf)) {
            memcpy(s_i2c_buf + s_i2c_buf_len, arg_ptr, arg_int);
            s_i2c_buf_len += arg_int;
        }
        break;

    case U8X8_MSG_BYTE_END_TRANSFER: {
        if (s_i2c_buf_len == 0) break;
        for (size_t i = 0; i < s_i2c_buf_len; i++) s_cmd_buf[i] = s_i2c_buf[i];
        s_cmd_buf[s_i2c_buf_len - 1] |= (1u << 9); /* STOP on the last byte */

        uint8_t addr7 = u8x8_GetI2CAddress(u8x8) >> 1;
        i2c_get_hw(OLED_I2C_PORT)->enable = 0;
        i2c_get_hw(OLED_I2C_PORT)->tar    = addr7;
        i2c_get_hw(OLED_I2C_PORT)->enable = 1;

        dma_channel_set_read_addr(s_dma_ch, s_cmd_buf, false);
        dma_channel_set_trans_count(s_dma_ch, s_i2c_buf_len, true); /* kicks off the transfer, returns immediately */
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
        vTaskDelay(pdMS_TO_TICKS(arg_int)); /* yields to other tasks, unlike sleep_ms() */
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
