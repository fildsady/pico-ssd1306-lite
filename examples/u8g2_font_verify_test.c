/* On-board unit test verifying u8g2 font DATA integrity (not just "looks ok
 * on screen") — for each font, walks every ASCII code 0x20-0x7E and checks:
 *   - u8g2_IsGlyph() confirms the glyph actually exists (catches a font
 *     missing a character it should have, e.g. the kind of gap a bad
 *     re-extraction/edit could introduce — see the Font12x16 line-splice
 *     bug found in pico-ssd1306-lite's own hand-extracted font.h, which
 *     silently dropped the ']' glyph and shifted every glyph after it).
 *   - u8g2_GetGlyphWidth() returns a sane positive width for every
 *     printable, non-space character (a corrupted/misaligned glyph table
 *     read tends to surface as a zero or wildly-wrong width here, since
 *     u8g2's font parser derives width from the glyph header it decodes at
 *     that offset).
 * Reports a per-font PASS/FAIL + bad-glyph count on the OLED (own Font6x10
 * label line) and holds each result on screen for 3s before moving to the
 * next font, so a failure is readable without needing a serial log. */
#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u8g2.h"
#include "u8g2_pico_hal.h"

typedef struct {
    const char *name;
    const uint8_t *font;
} FontEntry;

static const FontEntry s_fonts[] = {
    { "6x10",       u8g2_font_6x10_tr },
    { "ncenB08",    u8g2_font_ncenB08_tr },
    { "logisoso16", u8g2_font_logisoso16_tr },
    { "profont22",  u8g2_font_profont22_tr },
    { "helvB18",    u8g2_font_helvB18_tr },
};
#define FONT_COUNT (sizeof(s_fonts) / sizeof(s_fonts[0]))

/* Required by FreeRTOSConfig.h (same as can-audio-remote's main.c). */
uint32_t freertos_get_runtime_counter(void) { return time_us_32(); }
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask; (void)pcTaskName;
    for (;;) tight_loop_contents();
}

/* Returns the number of bad glyphs found (0 = pass) in code range
 * [0x20, 0x7E]. Space (0x20) is allowed a width of 0 — it's legitimately
 * blank — every other printable character should have positive width. */
static int verify_font(u8g2_t *u8g2, const uint8_t *font) {
    u8g2_SetFont(u8g2, font);
    int bad = 0;
    for (uint16_t code = 0x20; code <= 0x7E; code++) {
        if (!u8g2_IsGlyph(u8g2, code)) {
            bad++;
            continue;
        }
        int8_t w = u8g2_GetGlyphWidth(u8g2, code);
        if (code != 0x20 && w <= 0) bad++;
    }
    return bad;
}

static void task_u8g2_font_verify(void *pv) {
    (void)pv;

    u8g2_t u8g2;
    u8g2_pico_hal_init(&u8g2); /* brings up the shared I2C/DMA driver + u8g2 setup */

    for (;;) {
        for (size_t i = 0; i < FONT_COUNT; i++) {
            int bad = verify_font(&u8g2, s_fonts[i].font);

            char line1[24], line2[24];
            snprintf(line1, sizeof(line1), "%s:", s_fonts[i].name);
            snprintf(line2, sizeof(line2), bad == 0 ? "PASS (0 bad)" : "FAIL (%d bad)", bad);

            u8g2_ClearBuffer(&u8g2);
            u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
            u8g2_DrawStr(&u8g2, 0, 10, line1);
            u8g2_DrawStr(&u8g2, 0, 25, line2);
            /* Also render the font itself on the same screen — a PASS on
             * the glyph-table check plus visibly-correct rendering here is
             * the strongest confirmation this font is fine. */
            u8g2_SetFont(&u8g2, s_fonts[i].font);
            u8g2_DrawStr(&u8g2, 0, 55, "AaBb8@!?");
            u8g2_SendBuffer(&u8g2);

            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}

int main(void) {
    stdio_init_all();
    xTaskCreate(task_u8g2_font_verify, "u8g2verify", 2048, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) tight_loop_contents();
}
