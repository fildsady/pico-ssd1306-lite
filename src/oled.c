/* See oled.h — deliberately has no MCU-specific includes (no pico/stdlib.h,
 * no hardware/*.h): every hardware detail lives behind the byte_cb/
 * gpio_and_delay_cb callbacks passed into oled_init(). */
#include "oled.h"

static u8g2_t s_u8g2;

void oled_init(u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb)
{
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&s_u8g2, U8G2_R0, byte_cb, gpio_and_delay_cb);
    u8g2_InitDisplay(&s_u8g2);
    u8g2_SetPowerSave(&s_u8g2, 0);
    oled_set_font(OLED_FONT_NORMAL);
}

void oled_set_font(OledFont font)
{
    /* 9x15/9x18B are this repo's own deliberately-minimal pick (a
     * monospace body font + a bold title font) — reach for oled_u8g2() if
     * a project needs a different font from u8g2's much larger table. */
    u8g2_SetFont(&s_u8g2, font == OLED_FONT_TITLE ? u8g2_font_9x18B_tr : u8g2_font_9x15_tr);
}

void oled_clear(void) { u8g2_ClearBuffer(&s_u8g2); }
void oled_send(void)  { u8g2_SendBuffer(&s_u8g2); }

void oled_draw_str(int x, int y, const char *s)
{
    u8g2_DrawStr(&s_u8g2, x, y, s);
}

void oled_draw_row(int y, const char *line, bool inverted)
{
    if (inverted) {
        u8g2_SetDrawColor(&s_u8g2, 1);
        u8g2_DrawBox(&s_u8g2, 0, y, 128, 16);
        u8g2_SetDrawColor(&s_u8g2, 0);
    }
    u8g2_DrawStr(&s_u8g2, 0, y + 12, line);
    if (inverted) u8g2_SetDrawColor(&s_u8g2, 1);
}

void oled_draw_row_rjust(int y, const char *left, const char *right, bool inverted)
{
    if (inverted) {
        u8g2_SetDrawColor(&s_u8g2, 1);
        u8g2_DrawBox(&s_u8g2, 0, y, 128, 16);
        u8g2_SetDrawColor(&s_u8g2, 0);
    }
    u8g2_DrawStr(&s_u8g2, 0, y + 12, left);
    if (right && right[0]) {
        int w = u8g2_GetStrWidth(&s_u8g2, right);
        u8g2_DrawStr(&s_u8g2, 128 - w - 4, y + 12, right);
    }
    if (inverted) u8g2_SetDrawColor(&s_u8g2, 1);
}

u8g2_t *oled_u8g2(void) { return &s_u8g2; }
