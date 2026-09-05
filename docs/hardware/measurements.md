# Hardware / radio measurements

Target: Waveshare ESP32-C6-Touch-LCD-1.9  
Reference SDK: ESP-IDF v6.1  
Status: harness committed; numeric results require a physical-board run and are intentionally not fabricated here.

## Capture method

For every module or mode transition, record at minimum:

- `esp_timer_get_time()` before/after init, start, stop and deinit;
- `esp_get_free_heap_size()` before/after;
- task stack high-water mark for any created task;
- configured/static RX buffer counts where relevant;
- whether deinit restores heap;
- first successful RX/event latency after a mode switch;
- handoff drop counters under a controlled scan interval.

The current `firmware/main/main.c` already logs basic heap/stack/time marks for scan-session primitives, handoff queue allocation and LCD init. Extend the same style rather than introducing a benchmark framework.

## Results table

| Module / phase | Init us | Stop/deinit us | Free heap before | Free heap after | Task stack HWM | RX/static buffers | Heap recovered after deinit | Notes |
|---|---:|---:|---:|---:|---:|---:|---|---|
| scan-session + smoke test | pending board run | n/a | pending | pending | pending | static mutex | n/a | verifies BUSY while session owns gate |
| native Wi-Fi handoff queues | pending board run | n/a | pending | pending | caller task | 4 x bounded slots | static | no per-packet malloc |
| native NimBLE handoff queues | pending board run | n/a | pending | pending | caller task | 8 x bounded slots | static | legacy GAP path first |
| native 802.15.4 handoff queues/callback | pending board run | n/a | pending | pending | ISR + worker | 8 x bounded copied slots | static | driver frame released in ISR |
| ST7789V2 panel | pending board run | pending | pending | pending | caller/LVGL later | SPI DMA descriptors | pending | 170x320, gap 35/0 |
| Wi-Fi driver | pending | pending | pending | pending | pending | record sdkconfig | pending | active scan + promisc separately |
| NimBLE host/controller | pending | pending | pending | pending | pending | record sdkconfig | pending | passive/active scan separately |
| IEEE 802.15.4 radio | pending | pending | pending | pending | ISR + worker | record IDF RX buffer setting | pending | channels 11..26 |
| LVGL | pending | pending | pending | pending | pending | draw buffers | pending | test with realistic partial buffer |
| FATFS/SDSPI | pending | pending | pending | pending | caller | FATFS/SPI buffers | pending | same SPI2 bus as LCD |
| OpenThread | pending | pending | pending | pending | pending | pending | pending | only after raw 802.15.4 baseline |
| Zigbee component | pending | pending | pending | pending | pending | pending | pending | only after raw 802.15.4 baseline |

## Required RF switch matrix run

Measure at least these transitions while the single product scan-session lock remains held across the whole sequence:

```text
Wi-Fi active scan -> Wi-Fi promiscuous
Wi-Fi promiscuous -> NimBLE scan
NimBLE scan -> IEEE 802.15.4 promiscuous
IEEE 802.15.4 promiscuous -> Wi-Fi/LAN focus
```

For each transition record which public stop/disable API was required, whether the next stack could remain initialized, transition latency, first-event latency and any handoff drops. Do not infer coexistence behavior from documentation when it can be measured on the board.
