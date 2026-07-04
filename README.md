# pico-ssd1306-lib

SSD1306 OLED driver for the Raspberry Pi Pico (RP2040/RP2350) SDK, extracted
from [can-audio-remote](https://github.com/fildsady/can-audio-remote).

- I2C + DMA, no bit-banging — a non-blocking DMA transfer runs in the
  background while your FreeRTOS task keeps running
- Two rendering paths sharing the same underlying I2C/DMA driver (safe to
  call both from the same task on the same physical display, see
  [inc/ssd1306.h](inc/ssd1306.h)'s `ssd1306_i2c_send_dma()` doc comment):
  - **[u8g2](https://github.com/olikraus/u8g2)** (bundled as a git submodule,
    `lib/u8g2`) — the recommended default. Large built-in font table and
    shape-drawing (arcs/circles/polygons/boxes).
  - **This repo's own minimal `ssd1306.c`/`font.h`** — a handful of small
    bitmap fonts (`Font6x8`/`7x10`/`8x16`/`8x8`/`12x16`/`12x24`/`16x32`) for
    when flash/RAM is tight and you don't need u8g2's font table.

## Dependencies

- Pico SDK (`hardware/i2c.h`, `hardware/dma.h`, `hardware/irq.h`)
- FreeRTOS (`FreeRTOS.h`, `task.h`, `semphr.h`) — the shared I2C/DMA driver
  assumes an RTOS is present (uses a semaphore to guard the buffer across
  tasks). Not intended for bare-metal use.

## Wiring / config

This driver never hardcodes your board's pins/I2C settings itself — same
pattern as `FreeRTOSConfig.h`: [inc/ssd1306.h](inc/ssd1306.h) does
`#include "oled_driver_config.h"` and expects **your project** to provide
that file somewhere on its include path. **The build won't compile without
it** — that's intentional, there's no sane default wiring to fall back to.

Copy [inc/oled_driver_config.example.h](inc/oled_driver_config.example.h)
into your project as `oled_driver_config.h` and fill in your board's pins:

```c
#define OLED_I2C_PORT   i2c1
#define OLED_I2C_SDA    2
#define OLED_I2C_SCL    3
#define OLED_I2C_ADDR   0x3C
#define OLED_I2C_FREQ   400000
#define OLED_DMA_IRQ_INDEX 1   /* DMA_IRQ_0 or DMA_IRQ_1 — pick whichever
                                   isn't already used by another driver in
                                   your project */
```

## Usage (oled.h — recommended)

[inc/oled.h](inc/oled.h) is a thin, MCU-agnostic wrapper around u8g2 with
`oled_*` names instead of `u8g2_*` — your application code doesn't need to
know u8g2 is the renderer underneath, and porting to another MCU later only
means writing a new HAL (matching `u8g2_pico_hal.c`'s callback signature)
for that platform; `oled.h`/`oled.c` themselves need no changes:

```c
#include "oled.h"
#include "u8g2_pico_hal.h"   /* Pico-specific HAL callbacks */
#include "ssd1306.h"         /* Pico-specific shared I2C/DMA driver */

ssd1306_init();
oled_init(u8g2_pico_hal_byte_cb, u8g2_pico_hal_gpio_and_delay_cb);
oled_clear();
oled_draw_str(0, 20, "Hello");
oled_draw_row(32, "> Selected item", true); /* highlight a selected menu row */
oled_send();
```

Reach for `oled_u8g2()` (returns the underlying `u8g2_t *`) for anything
this wrapper doesn't cover — custom fonts, shapes, etc — at the cost of
tying that code back to u8g2's own naming.

## Usage (raw u8g2)

If you don't need the MCU-agnostic wrapper, `u8g2_pico_hal_init()` brings
up the shared I2C/DMA driver and u8g2 itself in one call:

```c
#include "u8g2.h"
#include "u8g2_pico_hal.h"

u8g2_t u8g2;
u8g2_pico_hal_init(&u8g2);
u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
u8g2_ClearBuffer(&u8g2);
u8g2_DrawStr(&u8g2, 0, 20, "Hello");
u8g2_SendBuffer(&u8g2);
```

See `examples/u8g2_font_test.c` for a minimal cycling-fonts example, and
`examples/u8g2_font_verify_test.c` / `u8g2_gfx_verify_test.c` for real
on-board unit tests (not just eyeballing the screen) covering font-data
integrity and shape-drawing pixel correctness.

## Usage (minimal built-in fonts)

```c
#include "ssd1306.h"

ssd1306_init();
ssd1306_clear();
ssd1306_draw_string_8x16(0, 0, "Hello");
ssd1306_invert_rect(0, 0, 128, 16); /* e.g. highlight a selected menu row */
ssd1306_update();
```

See [inc/ssd1306.h](inc/ssd1306.h) for the full API. `ssd1306_init()` is
idempotent — safe to call even if `u8g2_pico_hal_init()` already called it,
so a project can mix both rendering paths on different screens.

## License

MIT — see [LICENSE](LICENSE). `Font8x16` in `font.h` is separately public domain (see the comment above it in that file for attribution).
