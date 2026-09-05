# Native scanner input contract (Agent D -> Agent B)

Target: ESP32-C6, ESP-IDF v6.1.

This is deliberately **not** a NearBy packet abstraction. Agent B consumes native ESP-IDF / ESP-NimBLE records. D only makes callback-scoped bytes durable where required and owns driver lifecycle, bounded queues, drop accounting and RF hardware exclusion.

## 1. Wi-Fi active scan

D owns:

```text
scan-session owner
 -> esp_netif / esp_wifi init
 -> WIFI_STORAGE_RAM
 -> active esp_wifi_scan_start
 -> esp_wifi_scan_get_ap_records
 -> B consumes caller-owned wifi_ap_record_t records
 -> stop/deinit before another RF phase
```

B receives `wifi_ap_record_t` directly. The records array is application-owned; B must not retain a pointer after the owning array is reused or freed.

A product discovery scan requires the complete scan-session gate. Portal Wi-Fi scans instead run only inside the temporary Web Management competing-operation lease.

## 2. Wi-Fi promiscuous RX

D registers the native `wifi_promiscuous_cb_t`. Treat the callback `buf`, `wifi_promiscuous_pkt_t` and payload as driver-owned and valid only until callback return.

D's async handoff is Wi-Fi-specific:

```c
typedef struct {
    wifi_promiscuous_pkt_type_t type;
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint16_t original_len;
    uint16_t captured_len;
    bool truncated;
    uint8_t payload[NEARBY_WIFI_CAPTURE_MAX];
} nearby_wifi_rx_record_t;
```

Rules:

- no per-packet heap allocation;
- callback does bounded checks/copy only;
- `WIFI_PKT_MISC` is not presented as a normal frame payload;
- RX-error frames are dropped;
- trailing FCS handling follows Espressif's simple-sniffer convention;
- queue overflow increments the Wi-Fi drop counter;
- a truncated frame is explicitly marked; it is never silently presented as complete;
- B parses the copied slot at task level and returns it with `nearby_wifi_rx_release()`.

Never queue the original driver pointer.

## 3. ESP-NimBLE discovery

ESP32-C6 v6.1 builds enable:

```text
CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=y
CONFIG_BT_NIMBLE_EXT_ADV=y
```

D initializes NimBLE, waits for host sync/address inference, starts GAP discovery, and tears the host/controller down before another D RF phase begins.

### Legacy reports

For `BLE_GAP_EVENT_DISC`, D copies the native `struct ble_gap_disc_desc` by value, copies bounded advertisement data, and repoints `desc.data` into the durable D slot.

The legacy path remains separate:

```c
typedef struct {
    struct ble_gap_disc_desc desc;
    uint16_t original_length_data;
    bool truncated;
    uint8_t data[NEARBY_BLE_LEGACY_DATA_MAX];
} nearby_ble_disc_record_t;
```

### Extended reports

When `MYNEWT_VAL(BLE_EXT_ADV)` is enabled, D uses `ble_gap_ext_disc()` and receives `BLE_GAP_EVENT_EXT_DISC`.

Extended reports use their own bounded queue and native descriptor:

```c
typedef struct {
    struct ble_gap_ext_disc_desc desc;
    uint16_t original_length_data;
    bool truncated;
    uint8_t data[NEARBY_BLE_EXT_DATA_MAX];
} nearby_ble_ext_disc_record_t;
```

D preserves native extended metadata including address, RSSI, TX power, SID, primary/secondary PHY, periodic interval, properties and `data_status`. `desc.data` is repointed to the durable slot buffer.

**Boundary:** D does not reassemble chained or incomplete extended-advertising reports. `COMPLETE`, `INCOMPLETE` and `TRUNCATED` controller status remains native input for B. Any semantic reassembly/parsing policy belongs above D.

Legacy and extended records are never collapsed into a generic BLE packet type.

### Callback lifetime

The GAP event, discovery descriptor's original data pointer, and parsed advertisement-field pointers are callback scoped. B must only retain D's copied record until the matching release call.

## 4. IEEE 802.15.4 receive

D follows the public driver lifecycle:

```text
register callback list while disabled
 -> esp_ieee802154_enable
 -> set channel 11..26
 -> promiscuous=true
 -> esp_ieee802154_receive
 -> ISR callback
 -> stop/sleep/disable
```

The RX callback is ISR context. D copies the native `esp_ieee802154_frame_info_t` plus the length-prefixed frame into a preallocated slot, then calls:

```c
esp_ieee802154_receive_handle_done(frame);
```

before leaving the ISR path. The driver-owned pointer never reaches B.

Queue-full, malformed-length and other drop paths must still return driver ownership and increment the 802.15.4 drop counter.

B receives only the copied native-facing record and owns Zigbee/Thread/Matter parsing above this boundary.

## 5. Product scan-session gate

ESP32-C6 has one shared 2.4 GHz RF subsystem. A complete discovery run therefore owns one product gate from first module through final cleanup:

```text
IDLE
 -> scan_session_begin()
SCANNING
 -> Wi-Fi phase
 -> teardown
 -> NimBLE phase
 -> teardown
 -> 802.15.4 phase
 -> teardown
 -> nearby_radio_scan_cleanup_all()
 -> scan_session_end()
IDLE
```

The gate is not released/reacquired between scanner modules.

Competing device/RF/Web operations fail immediately while the complete scan owns the gate. Web Management uses the same gate through a competing-operation lease and therefore also excludes a complete scan.

Driver callbacks/ISRs never take this application gate.

## 6. D-internal RF phase guard

The product scan gate alone does not prevent the same scan-owner task from accidentally starting two radio stacks simultaneously. D therefore has a small private radio-phase guard with only these states:

```text
NONE / WIFI / NIMBLE / I154
```

It is not exposed as a scanner scheduler and does not cross the D boundary. A phase start fails if another phase is still owned. The next phase becomes legal only after the previous driver has been proven down and its phase released.

## 7. Fatal cleanup semantics

A failed driver teardown that leaves D unable to prove the RF subsystem is down is **fatal for the current complete scan session**.

Rules:

1. the failing Wi-Fi/NimBLE/802.15.4 path latches a scan fatal error when the current task owns the scan;
2. `scan_session_end()` refuses to release the product gate while fatal is latched;
3. competing RF/Web/device operations therefore remain excluded;
4. `nearby_radio_scan_cleanup_all()` retries all three teardown paths, including partial-init states;
5. only when every teardown succeeds and the private RF phase is `NONE` may D clear the fatal latch;
6. only then may the owner call `scan_session_end()`.

D never force-releases a mutex owned by another task. A returned cleanup error is not permission to proceed with a different radio stack.

## 8. Drop / overload policy

All hot receive paths use bounded preallocated slots. When a producer outruns its consumer:

- increment the transport-specific drop counter;
- do not block the Wi-Fi driver task, NimBLE host task or 802.15.4 ISR waiting for space;
- release driver ownership immediately where required;
- expose counts to measurement/bench logs.

Current D queues remain independent for Wi-Fi, BLE legacy, BLE extended and IEEE 802.15.4.

## 9. Ownership summary

Agent D owns native driver lifetime, callback lifetime safety, bounded copies, RF exclusion and hardware errors.

Agent B owns Wi-Fi frame parsing, BLE AD/service/manufacturer parsing, Zigbee/Thread/Matter parsing and scanner semantics.

Agent C owns recognition keys/database semantics.

Agent A owns Device/Entity/State semantics.

Agent E owns UI/Web presentation.

No protocol recognition or vendor matching is permitted in this component.

## Upstream anchors

- ESP-IDF v6.1 Wi-Fi public APIs and `examples/network/simple_sniffer`.
- ESP-IDF v6.1 NimBLE central / periodic-sync examples plus the pinned ESP-NimBLE `ble_gap.h` for `ble_gap_ext_disc()` and `ble_gap_ext_disc_desc`.
- ESP-IDF v6.1 IEEE 802.15.4 public API for callback registration, receive and `receive_handle_done()`.
- Waveshare board examples only for board-specific wiring/init evidence; generic radio lifecycle stays on Espressif public APIs.
