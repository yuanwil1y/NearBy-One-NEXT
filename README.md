# NearBy One NEXT

NearBy One NEXT is a portable ESP32-C6 platform for discovering, inspecting, and interacting with nearby devices.

Target board: **Waveshare ESP32-C6-Touch-LCD-1.9**  
Framework: **ESP-IDF + FreeRTOS + LVGL**  
Architecture baseline: **Native API + Kismet API**

> This branch starts the architecture reset of NearBy One NEXT.
>
> The firmware is no longer organized as one scanner application with private wrapper APIs. It is being rebuilt as a small handheld platform where applications may use ESP-IDF / FreeRTOS / LVGL directly, use higher-level `kismet_*` helpers, or freely combine both.

---

## 1. Product direction

NearBy One NEXT is intended to become a small, open, application-oriented handheld platform built around the ESP32-C6 radio stack, touch display, removable storage, and a growing toolbox of reusable nearby-device capabilities.

The long-term product idea is closer to a **programmable wireless Swiss-army knife** than a single-purpose scanner.

The firmware therefore has two equally supported development paths:

```text
Application
   │
   ├────────────── Native path ──────────────┐
   │                                         │
   │                                         ▼
   │                              ESP-IDF / FreeRTOS / LVGL
   │
   └────────────── Kismet path ──────────────┐
                                             │
                                             ▼
                                      kismet_* toolbox
                                             │
                                             ▼
                                  ESP-IDF / FreeRTOS / LVGL
```

Neither path is privileged. Applications may mix them in the same module.

---

## 2. Architecture constitution

These rules are the development baseline for this branch.

1. **ESP-IDF, FreeRTOS, and LVGL native APIs remain first-class public APIs for applications.**
2. **Do not wrap a native API only to rename it.**
3. **Board/BSP code exists only for board-specific hardware knowledge and bring-up.**
4. **All project-owned high-level convenience APIs use the `kismet_*` namespace.**
5. **A `kismet_*` API must combine meaningful behavior, state management, parsing, orchestration, or workflow logic.**
6. **Kismet does not permanently own hardware resources. It borrows them, coordinates conflicts, and restores state when practical.**
7. **Applications may freely combine native APIs and Kismet APIs.**
8. **LVGL remains LVGL. Do not create a second NearBy UI widget API that mirrors `lv_*`.**
9. **FreeRTOS remains FreeRTOS. Do not create NearBy task/queue/semaphore wrappers without a real product-level reason.**
10. **Existing scanner code is migration source material, not an architectural constraint.**

A useful test is:

> If a proposed `kismet_xxx()` only calls one native function with the same meaning, it probably should not exist.

Bad:

```c
esp_err_t kismet_wifi_stop(void)
{
    return esp_wifi_stop();
}
```

Good:

```c
esp_err_t kismet_wifi_scan(const kismet_wifi_scan_config_t *config,
                           kismet_wifi_scan_result_t *result);
```

A real Kismet scan helper may coordinate current Wi-Fi state, acquire a shared resource lease, configure scanning, collect results, normalize them, restore the prior state, and release the lease.

---

## 3. Layer 1 — Native platform

Layer 1 is intentionally **not a NearBy abstraction layer**.

Applications and Kismet modules directly use upstream APIs such as:

```text
ESP-IDF
├─ esp_wifi_*
├─ esp_netif_*
├─ esp_event_*
├─ esp_lcd_*
├─ esp_ieee802154_*
├─ GPIO / SPI / I2C / USB / SD / FATFS
└─ NimBLE APIs

FreeRTOS
├─ xTaskCreate / vTaskDelete
├─ Queue
├─ Semaphore / Mutex
├─ EventGroup
└─ Timer

LVGL
├─ lv_obj_*
├─ lv_label_*
├─ lv_button_*
├─ lv_event_*
├─ lv_timer_*
└─ the rest of upstream LVGL
```

### What belongs in the BSP

The target board contains facts that upstream ESP-IDF cannot know automatically, including pin assignments, shared buses, display geometry, reset/power sequencing, and touch/display wiring.

Those facts belong in a narrow BSP.

Target BSP responsibilities:

```text
BSP
├─ board pin definitions
├─ shared SPI/I2C initialization
├─ LCD bring-up
├─ touch-controller bring-up
├─ SD-card attachment
├─ backlight / board power details
└─ access to native ESP-IDF handles
```

The BSP should expose native handles whenever possible rather than inventing replacement handle types.

It must not contain scanner semantics, protocol parsing, discovery workflows, device recognition, or application UI logic.

---

## 4. Layer 2 — Kismet toolbox

Layer 2 is the project-owned reusable capability library.

The Kismet layer is a **toolbox of recipes and workflows**, not a HAL.

Its public API uses the namespace:

```c
kismet_<domain>_<action>()
```

Examples of intended API families:

```text
kismet_wifi_scan()
kismet_wifi_monitor_start()
kismet_wifi_monitor_stop()
kismet_wifi_channel_survey()

kismet_ble_scan()
kismet_ble_parse_advertisement()

kismet_i154_scan()

kismet_mdns_discover()
kismet_ssdp_discover()
kismet_dhcp_discover()

kismet_capture_start()
kismet_capture_stop()

kismet_device_recognize()

kismet_scan_start()
kismet_scan_cancel()
kismet_scan_all()
```

Not every example above exists yet. They describe the direction and naming contract.

### Kismet modules may compose other Kismet modules

Small tools should remain independently reusable while larger workflows can compose them.

For example:

```text
kismet_wifi_scan()
       ┐
kismet_ble_scan()
       ├──> kismet_scan_all()
kismet_mdns_discover()
       │
kismet_ssdp_discover()
       ┘
```

The goal is to make common tasks easy without hiding the underlying platform from advanced applications.

---

## 5. Application layer

Applications are the actual product experiences shown to the user.

An application can be written entirely with native APIs:

```c
esp_wifi_scan_start(NULL, true);

lv_obj_t *label = lv_label_create(lv_screen_active());
lv_label_set_text(label, "Scanning...");
```

Or primarily with Kismet:

```c
kismet_wifi_scan(&config, &result);
```

Or both:

```c
kismet_wifi_scan(&config, &result);

/* special behavior not provided by Kismet */
esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

/* native UI */
lv_label_set_text_fmt(label, "%u APs", result.count);
```

This mixed model is intentional and supported.

---

## 6. Resource ownership and coexistence

Opening both API paths means resource ownership must be explicit.

Shared resources include at least:

- the ESP32-C6 2.4 GHz radio;
- Wi-Fi driver state;
- NimBLE host/controller state;
- IEEE 802.15.4 receive state;
- the LCD/SD shared SPI bus;
- SD filesystem access;
- LVGL/UI task ownership.

Kismet APIs must not assume they own the entire device.

A high-level helper should, where practical:

```text
inspect current state
        ↓
acquire the minimum required lease/lock
        ↓
perform the workflow
        ↓
restore previous state
        ↓
release the lease/lock
```

A small resource coordinator may exist for ownership, leases, mutexes, cancellation, and conflict control.

It must **not** become another hardware abstraction API.

Good:

```text
resource_acquire(WIFI)
resource_release(WIFI)
```

Bad:

```text
resource_wifi_scan()
resource_ble_start()
```

The latter simply recreates another wrapper layer.

---

## 7. Target source layout

The tree will migrate toward a structure similar to:

```text
firmware/
├─ components/
│  ├─ bsp/
│  │  └─ waveshare_esp32c6_touch_lcd_1_9/
│  │
│  ├─ lvgl_port/
│  │
│  ├─ kismet/
│  │  ├─ core/
│  │  ├─ wifi/
│  │  ├─ ble/
│  │  ├─ ieee802154/
│  │  ├─ network/
│  │  ├─ capture/
│  │  ├─ parser/
│  │  ├─ recognition/
│  │  └─ storage/
│  │
│  └─ app_runtime/
│
└─ main/
   └─ main.c

apps/
├─ scanner/
├─ wifi_analyzer/
├─ ble_explorer/
└─ ...
```

The exact directory split may evolve, but the dependency direction must remain:

```text
App
├──────────────> Native APIs
│
└──> Kismet ───> Native APIs

BSP ───────────> Native APIs
```

The native layer must never depend on Kismet or applications.

---

## 8. Migration from the current firmware

The existing integrated scanner firmware contains valuable, tested behavior. It should be migrated rather than discarded wholesale.

However, old component boundaries are not automatically preserved.

Initial mapping:

| Current area | New direction |
|---|---|
| `nearby_board` | narrow BSP / board bring-up |
| `nearby_lvgl_port` | thin `lvgl_port` |
| `radio_runtime` | remove wrapper role; native calls move directly into Apps/Kismet |
| `scan_coordinator` | migrate into Kismet scan/session workflow |
| `scan_session` | migrate into Kismet session/resource logic |
| `discovery_parser` | migrate into Kismet parser/network modules |
| `nearby_scan_pipeline` | split between Kismet composition and application logic |
| recognition/database code | retain as reusable bounded capability; expose through the new architecture |
| `main.c` | reduce to boot/platform/app-runtime startup |

During migration, temporary compatibility code is acceptable only when clearly marked and scheduled for removal.

The desired steady state is that `main.c` becomes small:

```c
void app_main(void)
{
    board_init();
    lvgl_port_init();

    app_runtime_init();
    app_runtime_start();
}
```

Scanner workflows, protocol parsing, recognition, storage operations, and UI pages should not accumulate in `main/`.

---

## 9. Kismet migration policy

The Kismet toolbox should be built primarily by migrating useful functionality from the existing scanner workstreams and scanner projects.

Migration priorities:

1. preserve useful protocol/scanner behavior;
2. remove redundant NearBy wrappers around ESP-IDF/FreeRTOS/LVGL;
3. isolate reusable workflow logic;
4. define small stable `kismet_*` public APIs;
5. make resource ownership explicit;
6. add tests around the migrated behavior;
7. allow applications to bypass Kismet whenever direct native control is preferable.

Kismet is not required to cover every native feature.

A missing Kismet helper is not an architectural defect because the native API remains available.

---

## 10. Hardware baseline

Target board: **Waveshare ESP32-C6-Touch-LCD-1.9**

Current verified baseline:

| Function | GPIO / detail |
|---|---|
| LCD resolution | 170 × 320 portrait |
| LCD controller | ST7789V2 baseline |
| LCD MOSI / SD MOSI | GPIO4 |
| LCD SCLK / SD CLK | GPIO5 |
| LCD DC | GPIO6 |
| LCD CS | GPIO7 |
| LCD RST | GPIO14 |
| LCD backlight | GPIO15 |
| SD MISO | GPIO19 |
| SD CS | GPIO20 |
| I2C SCL | GPIO8 |
| I2C SDA | GPIO18 |
| Touch | CST816, address `0x15` |
| MCU target | `esp32c6` |
| Flash | 8 MB board baseline |
| PSRAM | none assumed |

LCD and SD share `SPI2_HOST`, so the BSP must initialize and own the bus topology coherently rather than allowing independent drivers to repeatedly create/free the same bus.

---

## 11. Build baseline

The firmware remains a normal ESP-IDF project.

Typical build flow:

```bash
cd firmware
idf.py set-target esp32c6
idf.py build
```

Flash/monitor:

```bash
idf.py -p <PORT> flash monitor
```

The architecture reset should remain buildable throughout migration. Large rewrites should be split into stages rather than leaving the branch broken for long periods.

---

## 12. Development phases

### Phase 0 — Architecture baseline

This branch begins here.

- establish Native + Kismet architecture;
- replace the old README/agent assumptions;
- define module boundaries and naming rules;
- keep existing firmware as migration input.

### Phase 1 — Native foundation cleanup

- reduce BSP to board-specific responsibilities;
- remove redundant ESP-IDF wrappers;
- keep LVGL port thin;
- define shared resource/ownership rules;
- shrink `main/`.

### Phase 2 — Kismet extraction

- migrate Wi-Fi scanner capabilities;
- migrate BLE capabilities;
- migrate 802.15.4 capabilities;
- migrate LAN discovery/parsers;
- migrate capture, recognition, and reusable storage workflows;
- provide stable `kismet_*` headers.

### Phase 3 — App runtime

- define application lifecycle;
- move scanner UI into an application;
- allow additional applications to use native, Kismet, or mixed APIs;
- build navigation/launcher behavior appropriate for the handheld device.

### Phase 4 — Platform expansion

Future work may add additional tools, applications, accessories, radios, and workflows while preserving the same two-path API model.

---

## 13. Security boundary

NearBy One NEXT is intended for legitimate device discovery, diagnostics, experimentation, development, and authorized interaction.

The baseline may support passive observation, protocol parsing, discovery, packet capture where legally/technically appropriate, and user-authorized device interaction.

Do not add features whose primary purpose is credential theft, unauthorized access, persistence on third-party devices, destructive interference, or bypassing access controls.

---

## 14. Definition of a good new API

Before adding a public API, ask:

```text
Is this functionality already directly available from ESP-IDF / FreeRTOS / LVGL?
    │
    ├─ yes, and we are only renaming it
    │      └─ do not add the wrapper
    │
    └─ no / meaningful workflow is being added
           └─ consider a kismet_* API
```

A good `kismet_*` API normally adds at least one of:

- multi-step orchestration;
- resource arbitration;
- state preservation/restoration;
- normalization;
- parsing;
- protocol-specific convenience;
- bounded collection/results;
- cancellation/progress;
- composition of multiple native subsystems.

---

## 15. Baseline status

This README describes the **target architecture of the refactor branch**.

The source tree is currently in transition. Existing `nearby_*` components may still implement the previous integrated scanner architecture until they are migrated or removed.

For new development on this branch:

> **Native APIs are the foundation. `kismet_*` APIs are reusable shortcuts. Applications can use both.**

That is the new NearBy One NEXT development baseline.
