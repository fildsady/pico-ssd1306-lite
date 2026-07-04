/* On-board sanity test for the u8g2 integration (see u8g2_pico_hal.h) — runs
 * as a real FreeRTOS task (not a bare main() loop) so the DMA/semaphore HAL
 * is exercised under the same conditions the real firmware would use it in,
 * not a reduced/idle-CPU bare-metal spec. Cycles a handful of u8g2's
 * built-in fonts on the OLED, one per screen, ~2s each, same "AaBb8@!?"
 * sample string used for pico-ssd1306-lite's own font test (see
 * can-audio-remote's src/test_fonts.c) so any glyph mangling is just as
 * visible here. u8g2's font table is a single well-established upstream
 * source (not hand re-extracted per project), so this is mainly confirming
 * the HAL wiring is correct, not hunting for font-data bugs. */
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

/* Required by FreeRTOSConfig.h (same as can-audio-remote's main.c) — this
 * standalone test has no watchdog/checkpoint system to feed into either
 * hook, so both are trivial here. */
uint32_t freertos_get_runtime_counter(void) { return time_us_32(); }
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask; (void)pcTaskName;
    for (;;) tight_loop_contents();
}

static void task_u8g2_font_test(void *pv) {
    (void)pv;

    u8g2_t u8g2;
    u8g2_pico_hal_init(&u8g2); /* brings up the shared I2C/DMA driver + u8g2 setup */

    int i = 0;
    for (;;) {
        u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_6x10_tr); /* fixed small font for the label row */
        u8g2_DrawStr(&u8g2, 0, 10, s_fonts[i].name);
        u8g2_SetFont(&u8g2, s_fonts[i].font);
        u8g2_DrawStr(&u8g2, 0, 40, "AaBb8@!?");
        u8g2_SendBuffer(&u8g2);

        i = (i + 1) % FONT_COUNT;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

int main(void) {
    stdio_init_all();
    xTaskCreate(task_u8g2_font_test, "u8g2test", 2048, NULL, 1, NULL);
    vTaskStartScheduler();
    for (;;) tight_loop_contents();
}
