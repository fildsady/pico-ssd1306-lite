/* On-board unit test verifying u8g2's shape-drawing primitives against the
 * actual pixel data in its framebuffer (not just "looks ok on screen") —
 * same idea as u8g2_font_verify_test.c, applied to graphics instead of
 * fonts. Uses u8g2_GetBufferPtr() (only valid in full-buffer "_f" mode,
 * which u8g2_Setup_ssd1306_i2c_128x64_noname_f() below uses) to read back
 * exactly the bits u8g2's drawing functions set, then checks specific
 * known pixels — corners/edges that must be lit, interior/exterior points
 * that must not be — against what each shape's geometry actually requires.
 *
 * Each sub-test draws one shape into a freshly-cleared buffer, checks its
 * pixels, and reports a running pass/fail tally + which shape (if any)
 * failed, on the OLED itself (Font6x10, known-good from the font test). */
#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u8g2.h"
#include "u8g2_pico_hal.h"

/* Same page-based layout as ssd1306.c's own back_buf and u8x8's tile
 * buffer: byte index = x + (y/8)*width, bit = y%8. */
static bool get_pixel(u8g2_t *u8g2, int x, int y) {
    uint8_t *buf = u8g2_GetBufferPtr(u8g2);
    int width = u8g2_GetDisplayWidth(u8g2);
    int page = y / 8, bit = y % 8;
    return (buf[page * width + x] >> bit) & 1;
}

typedef struct {
    const char *name;
    bool (*run)(u8g2_t *u8g2, char *fail_detail, size_t fail_detail_size);
} GfxTest;

/* u8g2_DrawBox: filled 10x6 rect at (5,5) — every pixel inside must be lit,
 * a ring of pixels just outside the box must not be. */
static bool test_box(u8g2_t *u8g2, char *fail, size_t fail_size) {
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawBox(u8g2, 5, 5, 10, 6); /* covers x:5-14, y:5-10 */
    for (int y = 5; y < 11; y++) {
        for (int x = 5; x < 15; x++) {
            if (!get_pixel(u8g2, x, y)) {
                snprintf(fail, fail_size, "interior (%d,%d) off", x, y);
                return false;
            }
        }
    }
    if (get_pixel(u8g2, 4, 7) || get_pixel(u8g2, 15, 7) ||
        get_pixel(u8g2, 9, 4) || get_pixel(u8g2, 9, 11)) {
        snprintf(fail, fail_size, "bleed outside box");
        return false;
    }
    return true;
}

/* u8g2_DrawFrame: outline-only 10x6 rect at (5,5) — border pixels lit,
 * center (interior) must NOT be lit, unlike DrawBox. */
static bool test_frame(u8g2_t *u8g2, char *fail, size_t fail_size) {
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawFrame(u8g2, 5, 5, 10, 6); /* x:5-14, y:5-10 */
    if (!get_pixel(u8g2, 5, 5) || !get_pixel(u8g2, 14, 5) ||
        !get_pixel(u8g2, 5, 10) || !get_pixel(u8g2, 14, 10)) {
        snprintf(fail, fail_size, "corner missing");
        return false;
    }
    if (get_pixel(u8g2, 9, 7)) { /* dead center — must be hollow */
        snprintf(fail, fail_size, "center (9,7) should be empty");
        return false;
    }
    return true;
}

/* u8g2_DrawHLine/DrawVLine: exact-length lines — every pixel along the run
 * lit, one pixel past each end must not be (catches an off-by-one length). */
static bool test_lines(u8g2_t *u8g2, char *fail, size_t fail_size) {
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawHLine(u8g2, 10, 20, 15); /* x:10-24 @ y=20 */
    u8g2_DrawVLine(u8g2, 40, 10, 12); /* y:10-21 @ x=40 */

    for (int x = 10; x < 25; x++) {
        if (!get_pixel(u8g2, x, 20)) {
            snprintf(fail, fail_size, "hline gap at x=%d", x);
            return false;
        }
    }
    if (get_pixel(u8g2, 9, 20) || get_pixel(u8g2, 25, 20)) {
        snprintf(fail, fail_size, "hline overrun past its ends");
        return false;
    }
    for (int y = 10; y < 22; y++) {
        if (!get_pixel(u8g2, 40, y)) {
            snprintf(fail, fail_size, "vline gap at y=%d", y);
            return false;
        }
    }
    if (get_pixel(u8g2, 40, 9) || get_pixel(u8g2, 40, 22)) {
        snprintf(fail, fail_size, "vline overrun past its ends");
        return false;
    }
    return true;
}

/* u8g2_DrawDisc: filled circle, radius 8 centered at (32,32) — center must
 * be lit (solid fill), and a point well outside the radius (a corner of the
 * bounding box) must not be — catches a disc that's actually hollow/empty
 * or one that fills its whole bounding box instead of just the circle. */
static bool test_disc(u8g2_t *u8g2, char *fail, size_t fail_size) {
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawDisc(u8g2, 32, 32, 8, U8G2_DRAW_ALL);
    if (!get_pixel(u8g2, 32, 32)) {
        snprintf(fail, fail_size, "center not filled");
        return false;
    }
    if (get_pixel(u8g2, 32 - 8, 32 - 8) || get_pixel(u8g2, 32 + 8, 32 + 8)) {
        /* corner of the 17x17 bounding box, outside the actual circle */
        snprintf(fail, fail_size, "fills bounding box corner, not just circle");
        return false;
    }
    return true;
}

static const GfxTest s_tests[] = {
    { "DrawBox",   test_box },
    { "DrawFrame", test_frame },
    { "Lines",     test_lines },
    { "DrawDisc",  test_disc },
};
#define TEST_COUNT (sizeof(s_tests) / sizeof(s_tests[0]))

uint32_t freertos_get_runtime_counter(void) { return time_us_32(); }
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask; (void)pcTaskName;
    for (;;) tight_loop_contents();
}

static void task_u8g2_gfx_verify(void *pv) {
    (void)pv;

    u8g2_t u8g2;
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
        u8g2_pico_hal_byte_cb, u8g2_pico_hal_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    for (;;) {
        int passed = 0;
        for (size_t i = 0; i < TEST_COUNT; i++) {
            char detail[32] = "";
            bool ok = s_tests[i].run(&u8g2, detail, sizeof(detail));
            if (ok) passed++;

            /* Leave the just-drawn shape on screen (don't clear it again)
             * so the shape itself is visible alongside the verdict, then
             * overlay the result text. */
            char line1[24], line2[32];
            snprintf(line1, sizeof(line1), "%s:", s_tests[i].name);
            if (ok) snprintf(line2, sizeof(line2), "PASS");
            else    snprintf(line2, sizeof(line2), "FAIL %s", detail);

            u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
            u8g2_DrawStr(&u8g2, 60, 20, line1);
            u8g2_DrawStr(&u8g2, 60, 32, line2);
            u8g2_SendBuffer(&u8g2);

            vTaskDelay(pdMS_TO_TICKS(3000));
        }

        /* Summary screen */
        u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
        char summary[24];
        snprintf(summary, sizeof(summary), "%d/%d passed", passed, (int)TEST_COUNT);
        u8g2_DrawStr(&u8g2, 0, 20, "GFX verify:");
        u8g2_DrawStr(&u8g2, 0, 40, summary);
        u8g2_SendBuffer(&u8g2);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

int main(void) {
    stdio_init_all();
    xTaskCreate(task_u8g2_gfx_verify, "u8g2gfx", 2048, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) tight_loop_contents();
}
