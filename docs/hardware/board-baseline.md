# Waveshare ESP32-C6-Touch-LCD-1.9 board baseline

Status: first-pass hardware evidence, targeted at ESP-IDF. This file intentionally separates verified board facts from code that still needs bench validation.

## Evidence used

1. Waveshare official schematic: `ESP32-C6-Touch-LCD-1.9-Schematic.pdf` from the official Waveshare documentation resources page.
2. Waveshare official product documentation for `ESP32-C6-Touch-LCD-1.9`, which names the LCD driver as **ST7789V2**, resolution 170x320, and touch IC as **CST816**.
3. Waveshare official repository `waveshareteam/ESP32-C6-LCD-1.9`, main tree commit observed during this audit: `1fed15e32977de11afe1f710a0f5e915bfcc35a5`.
4. In that repository, `02_Example/Arduino/07_Hello_World_GFX/07_Hello_World_GFX.ino` explicitly constructs `Arduino_ST7789(..., 170, 320, 35, 0, 35, 0)` with the target board GPIOs.
5. The same repository's current ESP-IDF LVGL example (`02_Example/ESP-IDF/07_LVGL_Test/main/main.c`) calls a copied `esp_lcd_new_panel_sh8601()` wrapper, but the command table is ST7789-family looking and conflicts with the official product documentation and working Arduino example. Treat the SH8601 symbol as an example bug / misnamed vendor wrapper, not board identity.
6. Espressif upstream provides the public `esp_lcd_new_panel_st7789()` driver, so NearBy One does not need the Waveshare SH8601 shim.

## Settled LCD path

For this board, use the ESP-IDF public ST7789 driver path:

```text
SPI2_HOST
  -> esp_lcd_new_panel_io_spi()
  -> esp_lcd_new_panel_st7789()
  -> esp_lcd_panel_reset()
  -> esp_lcd_panel_init()
  -> esp_lcd_panel_set_gap(panel, 35, 0)
  -> esp_lcd_panel_disp_on_off(panel, true)
```

Panel geometry is 170x320 RGB565. The visible window starts at controller column offset 35 and row offset 0 in portrait orientation. Do not copy the `esp_lcd_sh8601.*` files into NearBy One.

The Waveshare ESP-IDF example manually adds `+35` to the LVGL X coordinates. NearBy One should prefer `esp_lcd_panel_set_gap(panel, 35, 0)` once during panel bring-up so LVGL and other callers can use 0..169 / 0..319 logical coordinates directly.

## Pin map verified against official schematic

| Function | ESP32-C6 GPIO | Notes |
|---|---:|---|
| LCD MOSI / DIN | 4 | Shared with SD MOSI |
| LCD SCLK | 5 | Shared with SD CLK |
| LCD DC | 6 | LCD only |
| LCD CS | 7 | LCD only |
| LCD RST | 14 | LCD only |
| LCD BL | 15 | Drives the board backlight transistor; board circuit makes the logical polarity active-low |
| SD MISO | 19 | SDSPI input |
| SD CS | 20 | SD only |
| I2C SCL | 8 | Shared peripheral I2C bus |
| I2C SDA | 18 | Shared peripheral I2C bus |
| QMI8658 INT1 | 1 | On-board IMU |
| QMI8658 INT2 | 2 | On-board IMU |
| USB D- | 12 | Native USB; avoid reuse while USB is active |
| USB D+ | 13 | Native USB; avoid reuse while USB is active |
| UART0 TX | 16 | Schematic U0TX |
| UART0 RX | 17 | Schematic U0RX |
| BOOT | 9 | Boot key |
| BAT ADC | 0 | Battery sensing |

The schematic also confirms LCD MOSI/CLK and SD MOSI/CLK share the same physical SPI wires. NearBy One must initialize SPI2 once with MISO=19 present, then attach LCD and SDSPI devices to that same bus. Do not initialize/free SPI2 independently in LCD and SD modules.

## Touch baseline

Official documentation identifies the touch controller as CST816. The Waveshare BSP uses I2C0 with:

```text
SCL GPIO8
SDA GPIO18
CST816 address 0x15
```

The panel connector exposes touch reset/interrupt signals through the board's auxiliary I/O arrangement, while SCL/SDA are on the shared board I2C net. The first pass should reuse the Waveshare CST816 transaction format instead of adding a new touch driver.

## SPI sharing rules for the first implementation

- Initialize `SPI2_HOST` exactly once.
- Include `miso_io_num = GPIO19` even if LCD comes up before SD.
- LCD and SD have independent CS lines (`GPIO7`, `GPIO20`).
- Let the ESP-IDF SPI master/SDSPI layers serialize individual bus transactions. Do not bit-bang a second lock around every SPI transaction.
- A higher-level display/storage throttle may still be needed after latency measurement because long SD transfers can delay LVGL flushes. Measure before adding policy.
- Never free the bus while either LCD or SD is mounted/attached.

## Bring-up acceptance check on hardware

Before calling this item fully bench-verified, flash a minimal test that:

1. initializes ST7789 via the public ESP-IDF driver;
2. applies `(35, 0)` gap;
3. draws distinct pixels/rectangles at all four logical corners;
4. toggles the backlight;
5. initializes CST816 and prints raw x/y while touching the four corners;
6. mounts an SD card on the already-initialized SPI2 bus.

If the four-corner pattern is aligned without LVGL-side coordinate hacks, the ST7789 + 35-column offset path is the canonical NearBy One BSP path.
