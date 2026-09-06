# Agent D ESP-IDF hardware audit harness

Target: Waveshare ESP32-C6-Touch-LCD-1.9.

Reference SDK for this first pass: **ESP-IDF v6.1**. The code intentionally sticks to public APIs that exist in the v6.1 tag, including `esp_lcd_new_panel_st7789()` and `esp_ieee802154_event_callback_list_register()`.

## Build

```sh
cd firmware
idf.py set-target esp32c6
idf.py build
```

Flash/monitor on the physical board:

```sh
idf.py -p <PORT> flash monitor
```

## What the current harness checks

At boot `main/main.c`:

1. initializes the exclusive scan-session gate;
2. proves a competing operation is rejected while the complete scan session owns the gate and accepted after release;
3. allocates the bounded Wi-Fi, NimBLE and IEEE 802.15.4 handoff queues;
4. registers the IEEE 802.15.4 RX callback while that subsystem is still disabled, as required by ESP-IDF;
5. initializes SPI2 once and brings up the LCD using the public ST7789 driver with the board's 35-column gap;
6. logs elapsed microseconds, free heap and current task stack high-water marks for the first-pass primitives.

No scanner parser is linked here. The callbacks only preserve driver data long enough for Agent B to consume it in worker/task context.

## Bench acceptance for the next hardware pass

The current connector-only work can be statically audited, but physical LCD/touch/RF timing cannot be truthfully measured without flashing the target board. On the board, capture the monitor log and then extend the harness with measured init/deinit/switch probes for Wi-Fi, NimBLE and IEEE 802.15.4. Record results in `docs/hardware/measurements.md`.

Do not replace the ST7789 path with the Waveshare `esp_lcd_sh8601.*` shim unless physical evidence contradicts the official schematic/docs plus the working Waveshare ST7789 Arduino example.
