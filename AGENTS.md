# Agent D — Hardware / ESP-IDF bottom layer

## Mission
Own the Waveshare ESP32-C6-Touch-LCD-1.9 hardware/BSP and verified native ESP-IDF data entrypoints consumed by scanners.

## Non-negotiable rules
- Target: Waveshare ESP32-C6-Touch-LCD-1.9, ESP-IDF.
- Prefer Waveshare official examples + Espressif official APIs/components; verify examples against schematic/docs/hardware.
- Hand Agent B native ESP-IDF/NimBLE structures plus exact buffer lifetime/context rules. **Do not invent a generic packet framework.**
- ESP32-C6 has one shared 2.4 GHz RF chain: Wi-Fi/BLE/802.15.4 operations require explicit arbitration.
- A complete scan session is exclusive: block device interaction/competing RF work until all scanner modules finish.
- Keep ISR callbacks tiny; copy/queue only what must outlive driver buffers.

## Own
- Board pin map/BSP: LCD, touch, SD, I2C, QMI8658 as needed.
- Resolve ST7789V2 vs erroneous-looking SH8601 ESP-IDF example; prefer verified hardware evidence.
- Wi-Fi active scan + promiscuous RX entrypoints.
- ESP-NimBLE advertisement scan/GATT native event entrypoints.
- IEEE 802.15.4 promiscuous RX/channel control and ISR-safe handoff.
- lwIP/esp_netif network plumbing used by local discovery.
- Temporary SoftAP + lightweight HTTP server support for Web Management.
- Wi-Fi scan/provisioning transport for the portal.
- Streaming database upload transport to SD; never buffer whole DB in RAM/flash.
- SD/FATFS mount/format behavior and LCD/SD shared-SPI constraints.
- RF/coexistence ownership primitives, init/deinit sequencing and scan-session lock.
- RAM/stack/init/deinit/switch-latency measurements.

## Known board baseline to verify
- LCD: docs/Arduino example say ST7789V2, 170x320; current ESP-IDF LVGL example uses SH8601 and must not be trusted blindly.
- LCD SPI2: MOSI GPIO4, CLK GPIO5, CS GPIO7, DC GPIO6, RST GPIO14.
- SD shares SPI2: MISO GPIO19, CS GPIO20.
- I2C0: SCL GPIO8, SDA GPIO18; touch address 0x15, expected CST816.

## First deliverables
1. Verify schematic/pins and settle LCD controller/offset/init path with evidence.
2. Produce a native-input contract for B: Wi-Fi promiscuous callback structures/lifetime; NimBLE GAP discovery event structures/lifetime; 802.15.4 receive callback/ISR/lifetime and `receive_handle_done` rules.
3. Implement minimal queue/ring handoff examples without a new abstraction layer.
4. Implement/measure exclusive radio scan-session ownership and reject competing operations while scanning.
5. Measure RAM/stack/init/deinit/switch latency for Wi-Fi, NimBLE, 802.15.4, LVGL, FATFS and relevant Zigbee/OpenThread components.
6. Prototype temporary SoftAP + HTTP endpoints sufficient for E's Web Portal and C's streaming DB import contract.
7. Verify SD format + streaming write while sharing SPI with LCD; document required locking/throttling.

## Do not own
- Protocol parsers/matcher behavior (B).
- Recognition DB semantics/format (C), beyond transporting and storing validated bytes.
- Device/Entity runtime model (A).
- UI/Web visual design (E).

## Definition of done for first pass
B can consume documented native observation inputs safely; the board can show LVGL, read touch, mount/format SD, start a temporary SoftAP/HTTP service, stream a test file to SD, and a measured RF lock prevents scanner/device-operation conflicts.