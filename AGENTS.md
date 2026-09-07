# NearBy One NEXT — Refactor Development Rules

## Canonical source of truth

On branch `refactor/native-kismet-baseline`, the root `README.md` is the canonical architecture and development baseline.

If older integration notes, frozen-agent documents, historical scanner architecture, PR descriptions, or legacy implementation details conflict with this branch's root README, the root README wins unless the user explicitly decides otherwise.

## Mission

Refactor NearBy One NEXT from a single integrated scanner firmware into an application-oriented ESP32-C6 handheld platform with two equally supported API paths:

```text
Application
├──────────────> ESP-IDF / FreeRTOS / LVGL native APIs
└──> kismet_* ─> ESP-IDF / FreeRTOS / LVGL native APIs
```

The target board remains **Waveshare ESP32-C6-Touch-LCD-1.9**.

## Architecture rules

1. Native ESP-IDF, FreeRTOS, NimBLE, IEEE 802.15.4, storage, and LVGL APIs remain directly available to applications.
2. Do not create wrappers whose main purpose is renaming a native API.
3. Keep BSP code limited to board-specific knowledge, hardware bring-up, shared-bus topology, and access to native handles.
4. All project-owned reusable high-level convenience/workflow APIs use the `kismet_*` namespace.
5. A `kismet_*` API must add meaningful orchestration, parsing, normalization, resource coordination, state preservation, bounded result handling, or other reusable behavior.
6. Kismet APIs do not permanently own hardware resources. They must coexist with applications using native APIs directly.
7. Do not create replacement UI, RTOS, radio, storage, or networking frameworks parallel to LVGL/FreeRTOS/ESP-IDF without a concrete product requirement approved by the user.
8. Existing scanner code is migration material. Preserve useful behavior, not obsolete component boundaries.
9. Keep dependency direction one-way: App -> Kismet -> Native, while App may also call Native directly.
10. Keep the tree buildable during staged migration.

## Resource ownership

The ESP32-C6 radio, Wi-Fi state, NimBLE state, IEEE 802.15.4 state, shared LCD/SD SPI bus, storage, and LVGL owner task are shared resources.

Kismet workflows should generally follow:

```text
inspect current state
-> acquire minimum required lease/lock
-> perform workflow
-> restore previous state when practical
-> release lease/lock
```

A small resource coordinator for leases/mutexes/cancellation is allowed. It must not become another HAL or mirror the native driver API.

## Migration direction

Current code should move roughly as follows:

```text
nearby_board          -> narrow BSP
nearby_lvgl_port      -> thin lvgl_port
radio_runtime         -> remove wrapper role; use native APIs directly
scan_coordinator      -> kismet scan/session workflow
scan_session          -> kismet resource/session logic
discovery_parser      -> kismet parser/network capabilities
nearby_scan_pipeline  -> split between Kismet composition and application logic
main.c                -> boot + platform/app runtime startup only
```

Recognition/database functionality should be retained where useful but exposed through the new dependency model rather than forcing applications through the previous scanner pipeline.

## Kismet API naming

Public project-owned high-level functions should follow:

```c
kismet_<domain>_<action>()
```

Examples:

```text
kismet_wifi_scan()
kismet_ble_scan()
kismet_mdns_discover()
kismet_device_recognize()
kismet_scan_start()
kismet_scan_all()
```

Do not add trivial aliases such as `kismet_wifi_stop()` when they merely forward to `esp_wifi_stop()`.

## BSP boundary

The BSP may own board-specific facts such as:

- Waveshare pin definitions;
- ST7789V2 display bring-up;
- CST816 touch bring-up;
- shared SPI2 initialization for LCD + SD;
- I2C board bus initialization;
- backlight/power/reset sequencing;
- native ESP-IDF handle exposure.

The BSP must not own scanning, protocol parsing, recognition, UI pages, or application workflows.

## LVGL rule

LVGL is exposed directly to applications.

A thin port may initialize the display/touch integration and enforce the LVGL owner-task rule, but do not build `nearby_ui_*` or `kismet_ui_*` wrappers that mirror ordinary `lv_*` widgets/functions.

## FreeRTOS rule

Applications and Kismet may directly use FreeRTOS tasks, queues, mutexes, event groups, and timers.

Do not create a second project-specific RTOS abstraction solely for naming consistency.

## Build baseline

Target:

- Framework: ESP-IDF
- MCU target: `esp32c6`
- Board: Waveshare ESP32-C6-Touch-LCD-1.9
- Display: 170x320 portrait, ST7789V2 baseline
- Touch: CST816
- No PSRAM assumed

Keep normal ESP-IDF build/flash behavior:

```text
cd firmware
idf.py set-target esp32c6
idf.py build
idf.py -p <PORT> flash monitor
```

Do not invent a custom firmware packaging path unless the hardware or release process actually requires it.

## Security boundary

Passive discovery, protocol parsing, diagnostics, packet observation/capture where appropriate, and authorized device interaction are within scope.

Do not add credential theft, unauthorized access, destructive RF interference, persistence on third-party devices, or access-control bypass features.

## Working principle

When deciding where code belongs, use this rule:

> Native APIs provide the primitives. Kismet provides reusable recipes. Apps provide product experiences.

That is the architecture to preserve throughout this refactor.
