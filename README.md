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

## License

MIT — see [LICENSE](LICENSE). `Font8x16` in `font.h` is separately public domain (see the comment above it in that file for attribution).
