#ifndef OLED_H
#define OLED_H
#include <stdbool.h>
#include "u8g2.h"

/* Platform-agnostic wrapper around u8g2 — hides u8g2_* naming from
 * application code (menu/UI code shouldn't need to know u8g2 is the
 * renderer underneath) and has ZERO MCU-specific dependencies itself: it
 * only calls u8g2 functions and takes the HAL byte/gpio-delay callbacks as
 * parameters instead of calling a platform init function by name.
 *
 * Porting to another MCU (e.g. STM32) later: write a new <mcu>_u8g2_hal.c
 * exposing the same u8x8 msg-callback signature as u8g2_pico_hal.c's
 * u8g2_pico_hal_byte_cb()/u8g2_pico_hal_gpio_and_delay_cb() (see
 * u8g2_pico_hal.h), bring up that MCU's I2C/DMA driver, then pass its
 * callbacks into oled_init() below — oled.h/oled.c need no changes at all.
 *
 * Usage (Pico, from within a FreeRTOS task):
 *   ssd1306_init();  // Pico-specific: shared I2C/DMA driver (ssd1306.h)
 *   oled_init(u8g2_pico_hal_byte_cb, u8g2_pico_hal_gpio_and_delay_cb);
 *   oled_clear();
 *   oled_draw_str(0, 20, "Hello");
 *   oled_send();
 */
typedef enum { OLED_FONT_NORMAL, OLED_FONT_TITLE } OledFont;

void oled_init(u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb);
void oled_set_font(OledFont font);
void oled_clear(void);
void oled_send(void);
void oled_draw_str(int x, int y, const char *s);

/* Draws `line` at (0, y+12) across a full 16px-tall row starting at `y`; if
 * `inverted`, fills the row with a solid box first and draws the text in
 * background color on top — the common "menu row, highlight when
 * selected" pattern. */
void oled_draw_row(int y, const char *line, bool inverted);

/* Escape hatch — direct access to the underlying u8g2_t for anything this
 * wrapper doesn't cover (custom fonts, shapes, etc). Using this ties the
 * calling code back to u8g2's own API/naming, same tradeoff as reaching
 * past any other abstraction layer. */
u8g2_t *oled_u8g2(void);

#endif
