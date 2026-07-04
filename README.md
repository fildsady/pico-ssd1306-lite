# pico-ssd1306-lite

Minimal SSD1306 OLED driver for the Raspberry Pi Pico (RP2040/RP2350) SDK, extracted from [can-audio-remote](https://github.com/fildsady/can-audio-remote).

- Double-buffered (draw into a back buffer, `ssd1306_update()` swaps + kicks off a DMA transfer, non-blocking)
- I2C + DMA, no bit-banging
- `font.h` includes:
  - `Font6x8` — 6x8px classic 5x7 font (with 2x pixel-doubling helper, `ssd1306_draw_string_2x()`)
  - `Font7x10` — 7x10px
  - `Font8x16` — 8x16px, public domain (Neven Boyanov's ssd1306xled/tinusaur). 8px/char divides a 128px-wide OLED evenly into 16 columns, useful if you want text sized close to a 16x2 character LCD.

## Dependencies

- Pico SDK (`hardware/i2c.h`, `hardware/dma.h`, `hardware/irq.h`)
- FreeRTOS (`FreeRTOS.h`, `task.h`, `semphr.h`) — this driver assumes an RTOS is present (uses a semaphore to guard the double buffer across tasks). Not intended for bare-metal use.

## Wiring / config

Pins and I2C settings are `#define`d directly in [inc/ssd1306.h](inc/ssd1306.h) — edit them for your board:

```c
#define OLED_I2C_PORT   i2c1
#define OLED_I2C_SDA    2
#define OLED_I2C_SCL    3
#define OLED_I2C_ADDR   0x3C
#define OLED_I2C_FREQ   400000
```

## Usage

```c
#include "ssd1306.h"

ssd1306_init();
ssd1306_clear();
ssd1306_draw_string_8x16(0, 0, "Hello");
ssd1306_invert_rect(0, 0, 128, 16); /* e.g. highlight a selected menu row */
ssd1306_update();
```

See [inc/ssd1306.h](inc/ssd1306.h) for the full API.

## Alternative: u8g2

This repo also bundles [u8g2](https://github.com/olikraus/u8g2) (as a git
submodule, `lib/u8g2`) plus a Pico SDK HAL (`inc/u8g2_pico_hal.h` /
`src/u8g2_pico_hal.c`) for projects that want u8g2's much larger built-in
font table and shape-drawing (arcs/circles/polygons) instead of this repo's
own minimal `ssd1306.c`/`font.h`. It's a separate rendering path — same
I2C bus/pins, no shared code — pick one or the other, not both on the same
display. See `examples/u8g2_font_test.c` for a minimal usage example
(cycles a few of u8g2's fonts on-screen).

```c
#include "u8g2.h"
#include "u8g2_pico_hal.h"

u8g2_t u8g2;
u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
    u8g2_pico_hal_byte_cb, u8g2_pico_hal_gpio_and_delay_cb);
u8g2_InitDisplay(&u8g2);
u8g2_SetPowerSave(&u8g2, 0);
u8g2_ClearBuffer(&u8g2);
u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
u8g2_DrawStr(&u8g2, 0, 20, "Hello");
u8g2_SendBuffer(&u8g2);
```

`u8g2_pico_hal.c` only needs Pico SDK's `hardware_i2c` — no FreeRTOS
dependency, unlike this repo's own `ssd1306.c`.

## License

MIT — see [LICENSE](LICENSE). `Font8x16` in `font.h` is separately public domain (see the comment above it in that file for attribution).
