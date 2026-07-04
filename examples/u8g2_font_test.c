/* On-board sanity test for the u8g2 integration (see u8g2_pico_hal.h) —
 * cycles a handful of u8g2's built-in fonts on the OLED, one per screen,
 * ~2s each, same "AaBb8@!?" sample string used for pico-ssd1306-lite's own
 * font test (see can-audio-remote's src/test_fonts.c) so any glyph mangling
 * (like the line-splice bug found earlier in font.h's hand-extracted fonts)
 * is just as visible here. u8g2's font table is a single well-established
 * upstream source (not hand re-extracted per project), so this is mainly
 * confirming the HAL wiring (u8g2_pico_hal.c) is correct, not hunting for
 * font-data bugs the way the font.h ones needed. */
#include "pico/stdlib.h"
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

int main(void) {
    stdio_init_all();

    u8g2_t u8g2;
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
        u8g2_pico_hal_byte_cb, u8g2_pico_hal_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    int i = 0;
    for (;;) {
        u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_6x10_tr); /* fixed small font for the label row */
        u8g2_DrawStr(&u8g2, 0, 10, s_fonts[i].name);
        u8g2_SetFont(&u8g2, s_fonts[i].font);
        u8g2_DrawStr(&u8g2, 0, 40, "AaBb8@!?");
        u8g2_SendBuffer(&u8g2);

        i = (i + 1) % FONT_COUNT;
        sleep_ms(2000);
    }
}
