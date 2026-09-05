# Hardware / radio measurements

Target: Waveshare ESP32-C6-Touch-LCD-1.9  
Reference SDK: ESP-IDF v6.1  
Status: executable harness committed; numeric results require a physical-board run and are intentionally not fabricated here.

Use `docs/hardware/BENCH_REQUIRED.md` as the execution sheet. All values below remain `PENDING_BENCH` until observed on the target board.

## Capture method

For every module or mode transition, record at minimum:

- `esp_timer_get_time()` before/after init, start, stop and deinit;
- `esp_get_free_heap_size()` and `esp_get_minimum_free_heap_size()`;
- task stack high-water mark for any created task;
- configured/static RX buffer counts where relevant;
- whether deinit restores heap;
- first successful RX/event latency after a mode switch;
- handoff record and drop counts under a controlled scan interval;
- SD write/read throughput and latency;
- LCD draw-call latency while SD is active.

The firmware harness logs the same primitives directly. Do not add a separate benchmark framework.

## Results table

| Module / phase | Init/start us | Stop/deinit us | Free heap before | Free heap after | Task stack HWM | RX/static buffers | Heap recovered | Notes |
|---|---:|---:|---:|---:|---:|---:|---|---|
| scan-session owner/fatal smoke | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | static mutex | n/a | fatal latch must block gate release |
| native Wi-Fi handoff | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | caller | 4 bounded frame slots | static | no per-packet malloc |
| NimBLE legacy handoff | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | caller | 8 bounded legacy slots | static | native `ble_gap_disc_desc` copied by value |
| NimBLE extended handoff | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | caller | 4 x 255-byte extended slots | static | native `ble_gap_ext_disc_desc`; preserve data_status; no D reassembly |
| native 802.15.4 handoff | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | ISR + worker | 8 bounded copied slots | static | driver frame released in ISR |
| ST7789V2 panel | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | caller/LVGL | SPI DMA descriptors | PENDING_BENCH | 170x320, logical gap 35/0 |
| CST816 | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | caller | i2c_master bus/device handles | PENDING_BENCH | raw geometry/orientation requires corner run |
| Wi-Fi active scan | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | driver + caller | record sdkconfig | PENDING_BENCH | count APs + first result latency |
| Wi-Fi promiscuous | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | Wi-Fi driver + consumer | 4 D handoff slots | PENDING_BENCH | record RX count/drop count |
| NimBLE host/EXT discovery | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | NimBLE host | record sdkconfig + D slots | PENDING_BENCH | uncoded + coded PHY parameters |
| IEEE 802.15.4 radio | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | ISR + worker | record IDF RX config | PENDING_BENCH | channels 11..26 |
| FATFS/SDSPI mount | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | caller | FATFS/SPI buffers | PENDING_BENCH | same SPI2 as LCD |
| SD 1 MiB write/read | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | caller | 4 KiB bench buffer | n/a | write/read KiB/s + exact verify |
| whole-SD destructive format | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | caller | bounded raw sector buffer | n/a | not a secure erase claim |
| temporary SoftAP + HTTP | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | httpd/Wi-Fi | record server/Wi-Fi config | PENDING_BENCH | phone connectivity required |
| DB streaming upload | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | httpd + caller | 2048-byte HTTP chunk + storage buffers | PENDING_BENCH | C finalize must precede promotion |
| LCD + SD contention | PENDING_BENCH | n/a | PENDING_BENCH | PENDING_BENCH | SD worker HWM PENDING_BENCH | shared SPI2 | n/a | max LCD draw-call us + SD KiB/s |
| 100-cycle lifecycle stress | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | PENDING_BENCH | existing subsystem buffers | PENDING_BENCH | scan/radio/SD plus repeated Web lifecycle |

## Required RF switch matrix

Measure these transitions while the single product scan-session lock remains held across the whole sequence:

```text
Wi-Fi active scan -> Wi-Fi promiscuous
Wi-Fi promiscuous -> NimBLE extended discovery
NimBLE discovery -> IEEE 802.15.4 promiscuous
IEEE 802.15.4 promiscuous -> Wi-Fi/LAN focus
```

For each transition record which public stop/disable API was required, transition latency, first-event latency, RF phase state and handoff drops. A next phase must remain rejected until the current phase has been proven down.

## Stress acceptance notes

The long run is not accepted merely because it reaches cycle 100. Compare:

- free heap at cycle 0, every 10 cycles and final;
- minimum free heap over the run;
- task stack high-water marks;
- accumulated Wi-Fi / BLE legacy / BLE extended / 802.15.4 drops;
- SD mount/unmount failures;
- Web start/stop failures;
- scan fatal-latch/recovery events.

Any unexplained monotonic resource loss or gate/RF phase left owned after a successful cleanup is a failure requiring code investigation, not a number to tune around.
