# Agent D BENCH_REQUIRED execution checklist

Target: Waveshare ESP32-C6-Touch-LCD-1.9  
SDK: ESP-IDF v6.1  
Scope: Agent D hardware / native driver boundary only

This document is the physical-board execution sheet. Items remain **BENCH_REQUIRED** until their result is recorded from the target board. Do not replace a missing measurement with an inferred value.

All bench Kconfig options default to `n`. Production/default boot must not start a discovery scan, SoftAP, destructive SD operation or stress loop merely because the harness exists.

## 0. Build gate before attaching hardware

From `firmware/` with ESP-IDF v6.1 exported:

```sh
idf.py set-target esp32c6
idf.py build
```

The branch also runs `.github/workflows/agent-d-idf61-build.yml` against ESP-IDF v6.1 for both the default config and an all-bench compile-only config. Do not proceed with a physical acceptance run from a red build.

For a board run:

```sh
idf.py menuconfig
# Component config -> NearBy One Agent-D bench harness
idf.py build flash monitor
```

Enable only the bench cases required for that run. Use a known disposable SD card whenever the destructive option is enabled.

## 1. LCD orientation / gap / inversion

Enable `CONFIG_NEARBY_AGENT_D_LCD_VISUAL_BENCH=y`.

Expected harness behavior:

- ST7789V2 is initialized through the production BSP path;
- five deterministic RGB565 bands are drawn over the full logical 170x320 surface;
- markers are drawn at all four logical corners;
- the BSP continues to own the 35-column controller gap and inversion setting.

Record:

- [ ] portrait image fills the intended 170x320 visible area;
- [ ] no 35-pixel horizontal displacement or clipped edge;
- [ ] all four corner markers are visible in the expected corners;
- [ ] red/green/blue ordering is physically correct;
- [ ] inversion produces the intended colors/background;
- [ ] no persistent corruption after repeated redraws;
- observed result / photo reference: `PENDING_BENCH`.

## 2. CST816 raw touch

Enable `CONFIG_NEARBY_AGENT_D_TOUCH_BENCH=y`.

The harness reads the real CST816 through ESP-IDF v6.1 `i2c_master` APIs for 30 seconds and logs raw coordinates. During the window touch center, then top-left, top-right, bottom-left and bottom-right.

Record:

- [ ] I2C device responds at 0x15 on GPIO18 SDA / GPIO8 SCL;
- [ ] press/release behavior is stable;
- [ ] raw X minimum / maximum: `PENDING_BENCH`;
- [ ] raw Y minimum / maximum: `PENDING_BENCH`;
- [ ] physical corner -> raw-coordinate mapping: `PENDING_BENCH`;
- [ ] whether E needs swap/invert transform above D: `PENDING_BENCH`;
- [ ] no I2C errors during 30-second interaction.

D reports raw coordinates only; screen/UI transforms remain above this boundary.

## 3. SD mount and non-destructive I/O

First enable `CONFIG_NEARBY_AGENT_D_BOARD_IO_SMOKE=y`, with destructive bench disabled.

Record:

- [ ] FATFS mounts on the same SPI2 bus already used by LCD;
- [ ] reported sector size / capacity are plausible for the inserted card;
- [ ] repeated mount/unmount succeeds;
- [ ] LCD remains operational after SD attach/detach.

Then enable `CONFIG_NEARBY_AGENT_D_SD_IO_BENCH=y`.

The harness writes, `fsync`s, reads, verifies and deletes a bounded temporary file. Record:

- write KiB/s: `PENDING_BENCH`;
- read KiB/s: `PENDING_BENCH`;
- write latency: `PENDING_BENCH`;
- read latency: `PENDING_BENCH`;
- [ ] byte verification passes;
- [ ] temporary file is removed.

## 4. Whole-SD destructive format

**Use only a disposable test SD card.** Enable `CONFIG_NEARBY_AGENT_D_SD_DESTRUCTIVE_BENCH=y` only for this run.

This bench directly exercises D's low-level board primitive and therefore deliberately bypasses the product authorization chain. Product behavior must instead follow:

```text
C preflight verdict: safe_to_format
        -> D RAM-only one-shot authorization
        -> separate explicit user confirmation
        -> D whole-SD destructive format
        -> bounded .part stream
        -> C finalize validation
        -> D rename/promote
```

Record:

- [ ] old partition layout/content is no longer mountable as before;
- [ ] resulting card mounts as the expected single FAT filesystem;
- format elapsed time: `PENDING_BENCH`;
- [ ] remove/reinsert and remount works;
- [ ] repeat with at least two representative SD card sizes;
- failure/recovery observations: `PENDING_BENCH`.

This is a destructive reformat test, not a secure-erase claim.

## 5. Native RF receive and handoff

Enable `CONFIG_NEARBY_AGENT_D_RADIO_SMOKE=y`.

Provide nearby traffic during the run: at least one visible Wi-Fi AP, BLE advertiser including an extended-advertising source when available, and an IEEE 802.15.4 transmitter on the selected channel.

Validate the sequence while one complete scan-session gate remains owned:

```text
Wi-Fi active scan
 -> Wi-Fi promiscuous RX
 -> Wi-Fi teardown
 -> NimBLE discovery (EXT_DISC when configured)
 -> NimBLE teardown
 -> IEEE 802.15.4 promiscuous receive
 -> 802.15.4 teardown
 -> cleanup_all
 -> release scan-session gate
```

Record for each phase:

- init/start latency: `PENDING_BENCH`;
- first event latency: `PENDING_BENCH`;
- stop/deinit latency: `PENDING_BENCH`;
- handoff records consumed: `PENDING_BENCH`;
- drop count: `PENDING_BENCH`;
- [ ] Wi-Fi callback records survive callback return only through D's copied slots;
- [ ] BLE legacy and extended records use separate native-facing queues;
- [ ] extended `data_status` is preserved; D does not reassemble chained reports;
- [ ] 802.15.4 driver buffer is released on success and overload/drop paths;
- [ ] next RF phase is rejected until current phase is fully torn down.

## 6. Fatal scan cleanup recovery

The non-hardware scan-session smoke already verifies that a fatal latch prevents `scan_session_end()`. The physical run must additionally provoke or observe real driver cleanup failures where practical (for example controlled fault injection in a debug build, not random power abuse).

Record:

- [ ] failed radio teardown latches fatal state;
- [ ] `scan_session_end()` refuses to release the gate while fatal is latched;
- [ ] competing RF/Web operations remain BUSY;
- [ ] retrying `nearby_radio_scan_cleanup_all()` eventually proves all three drivers down and RF phase `NONE`;
- [ ] only then can fatal be cleared and the scan gate released;
- actual injected failure and error code: `PENDING_BENCH`.

Do not force-release another task's mutex as a recovery shortcut.

## 7. Temporary SoftAP / HTTP / provisioning

Enable `CONFIG_NEARBY_AGENT_D_WEB_BENCH=y`.

The harness starts the existing production lower-layer Web Management backend, logs the temporary SSID/password/IP, leaves a 120-second interaction window, then stops it.

Using a phone/laptop during that window, record:

- [ ] client can join the temporary WPA2 SoftAP;
- [ ] `/api/status` responds;
- [ ] `/api/wifi/scan` returns nearby AP records;
- [ ] current-session STA credentials can be submitted and connection starts;
- [ ] credentials are not intentionally persisted by D;
- [ ] stopping Web Management tears down HTTP/AP/Wi-Fi and releases the competing gate;
- start latency: `PENDING_BENCH`;
- stop latency: `PENDING_BENCH`;
- free heap before/start/stop: `PENDING_BENCH`.

Also verify that a complete scan cannot begin while Web Management owns the competing lease and Web Management cannot start during a complete scan.

## 8. Product DB authorization + streaming upload

Run Web Management with C's real validation/preflight integration connected.

Required order:

1. Submit/inspect a source DB using C's validation semantics.
2. If and only if C reports `safe_to_format`, pass that verdict to D's preflight-result transport/API.
3. Confirm `/api/status` reports `db_format_authorized=true`.
4. Verify `/api/db/format?confirm=ERASE` fails before authorization and fails without the exact explicit confirmation.
5. Send the explicit confirmation after authorization; confirm whole-SD format occurs.
6. Stream the DB to `/api/db/upload`; observe bounded chunks written directly to `.part`.
7. Confirm C's finalize validation succeeds before `.part` is renamed to the final DB path.

Record:

- [ ] authorization is RAM-only and consumed before the destructive attempt;
- [ ] failed format retry requires fresh preflight + fresh confirm;
- [ ] portal restart clears stale unconsumed authorization;
- [ ] wrong byte count fails promotion;
- [ ] C finalize failure deletes/rejects `.part` and does not promote;
- [ ] interrupt upload mid-stream, reconnect, and confirm stale `.part` cleanup behavior;
- sustained upload KiB/s: `PENDING_BENCH`;
- peak/free heap during upload: `PENDING_BENCH`.

D does not define the DB schema, header acceptance, CRC/SHA policy or recognition semantics.

## 9. LCD + SD SPI2 contention

Enable `CONFIG_NEARBY_AGENT_D_SPI_CONTENTION_BENCH=y` with a writable test card.

The harness writes a bounded SD file from a worker while the main task repeatedly submits LCD stripe draws through the same SPI2 host. No extra application transaction mutex is introduced.

Record:

- [ ] no SPI driver errors/deadlock;
- [ ] no visible display tearing/freeze beyond acceptable flush delay;
- LCD draw-call maximum latency: `PENDING_BENCH`;
- SD write elapsed time / throughput: `PENDING_BENCH`;
- SD worker stack HWM: `PENDING_BENCH`;
- [ ] compare SD-only throughput vs contention throughput;
- [ ] only add transfer chunking/throttling if these measurements show a real need.

## 10. RAM / stack / lifecycle measurements

For baseline firmware and each enabled subsystem record:

- `esp_get_free_heap_size()` before init, after start, after stop/deinit;
- `esp_get_minimum_free_heap_size()`;
- created-task stack high-water marks;
- init/start/stop/deinit elapsed microseconds;
- first-event latency;
- handoff drop counters.

Required rows are maintained in `docs/hardware/measurements.md`. Do not tune queue sizes or task stacks from estimates alone.

## 11. Repeated lifecycle stress

Enable `CONFIG_NEARBY_AGENT_D_STRESS_BENCH=y` only for a supervised run with a non-critical SD card.

The harness performs:

- 100 complete scan-session/radio lifecycle cycles;
- 100 SD mount/unmount cycles;
- repeated temporary Web Management start/stop cycles;
- resource checkpoints every 10 scan cycles.

Record:

- [ ] all 100 scan gates acquire/release correctly;
- [ ] no fatal latch remains after successful cleanup;
- [ ] no RF phase remains owned between cycles;
- [ ] no monotonic heap loss beyond understood allocator behavior;
- [ ] no stack watermark approaches failure margin;
- [ ] no SD mount/unmount leak;
- [ ] no SoftAP/HTTP lifecycle leak;
- [ ] handoff queues recover after overload/drop conditions;
- [ ] no stale callback-owned Wi-Fi/BLE/802.15.4 pointer crosses the documented boundary;
- final heap/min-heap/stack values: `PENDING_BENCH`.

## Acceptance rule

A checkbox or numeric field stays `PENDING_BENCH` until it was actually observed on the Waveshare ESP32-C6-Touch-LCD-1.9. A compile-only CI pass is necessary but is not evidence for electrical behavior, RF reception, display orientation, touch geometry, SD reliability, phone connectivity, SPI contention or measured RAM/latency.
