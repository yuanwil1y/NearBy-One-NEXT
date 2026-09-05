# Native scanner input contract (Agent D -> Agent B)

This is deliberately **not** a NearBy packet abstraction. Agent B consumes the native ESP-IDF / ESP-NimBLE structures directly where their lifetime permits it. If data crosses a callback/task boundary, D copies only the native metadata plus bytes that would otherwise expire.

The first pass targets ESP32-C6 public APIs in current ESP-IDF. Re-check field availability against the exact ESP-IDF version selected for the firmware before freezing ABI assumptions.

## 1. Wi-Fi active scan

### Entry point

Headers: `esp_wifi.h`, `esp_wifi_types.h`

```c
esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_wifi_scan_get_ap_num(uint16_t *number);
esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records);
```

### B receives

Pass `wifi_ap_record_t` records directly to B during the worker call that owns the caller-allocated records array. Useful native fields include SSID, BSSID, primary channel, RSSI, authentication mode, cipher information and PHY capability flags as supplied by the selected ESP-IDF version.

### Lifetime / context

The records array is application-owned. No callback-lifetime issue exists after `esp_wifi_scan_get_ap_records()` returns. B must not retain a pointer after the owning array is released/reused.

### Arbitration

An active scan is RF work. It may run only inside the exclusive scan session, or as a standalone RF operation after acquiring the same gate. UI/device interaction paths must return busy while the full scan session owns the gate.

---

## 2. Wi-Fi promiscuous RX

### Entry point

Headers: `esp_wifi.h`, `esp_wifi_types.h`

```c
typedef void (*wifi_promiscuous_cb_t)(void *buf,
                                      wifi_promiscuous_pkt_type_t type);

esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb);
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter);
esp_err_t esp_wifi_set_promiscuous(bool en);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second);
```

For `WIFI_PKT_MGMT`, `WIFI_PKT_CTRL`, and `WIFI_PKT_DATA`, `buf` is a `wifi_promiscuous_pkt_t *`:

```c
typedef struct {
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint8_t payload[0];
} wifi_promiscuous_pkt_t;
```

`rx_ctrl.sig_len` describes the captured payload length for normal management/data/control frames. `wifi_pkt_rx_ctrl_t` carries native receive metadata such as RSSI, rate/PHY flags, channel and timestamp; exact fields are target/version dependent, so B should include the ESP-IDF header instead of duplicating the struct.

`WIFI_PKT_MISC` must be treated specially: there is no normal payload to parse.

### Callback context

Espressif documents that `wifi_promiscuous_cb_t` runs directly in the Wi-Fi driver task. It is **not an ISR**, but it must remain short because heavy work blocks the Wi-Fi driver task.

Allowed in callback:

- inspect `type` and `rx_ctrl`;
- perform fixed-bound checks;
- copy the frame into a preallocated ring/queue slot;
- signal a worker.

Do not in callback:

- allocate/free dynamically per packet;
- log/format every frame;
- run protocol parsers;
- touch LVGL;
- perform SD I/O;
- block on a mutex held by application code.

### Buffer lifetime contract

Treat `buf`, `wifi_promiscuous_pkt_t`, and `payload` as driver-owned and valid only for the duration of the callback. Agent B may parse synchronously in the callback only if the work is demonstrably tiny; otherwise copy `rx_ctrl`, `type`, payload length and payload bytes before returning.

Never queue the `buf` pointer itself.

### Minimal async handoff shape

Use a Wi-Fi-specific fixed ring, not a generic packet type:

```c
typedef struct {
    wifi_promiscuous_pkt_type_t type;
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint16_t captured_len;
    uint8_t bytes[NEARBY_WIFI_CAPTURE_MAX];
} nearby_wifi_rx_slot_t;
```

If `rx_ctrl.sig_len > NEARBY_WIFI_CAPTURE_MAX`, either drop or mark truncated according to the scanner's requirement. Never silently present a truncated frame as complete.

---

## 3. ESP-NimBLE GAP discovery

### Entry point

Headers from ESP-NimBLE host, normally `host/ble_gap.h`, `host/ble_hs.h`, and `host/ble_hs_adv.h`.

Legacy scan examples use:

```c
ble_gap_disc(own_addr_type, duration_ms, &disc_params, gap_event_cb, arg);
```

The registered callback receives:

```c
int gap_event_cb(struct ble_gap_event *event, void *arg);
```

For legacy advertising reports:

```c
event->type == BLE_GAP_EVENT_DISC
event->disc   /* struct ble_gap_disc_desc */
```

Agent B should consume the native discovery descriptor. Important members in current NimBLE examples are the peer address, RSSI, advertising data length and advertising data pointer. Parse AD structures with the native helper when useful:

```c
struct ble_hs_adv_fields fields;
ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
```

The parsed `ble_hs_adv_fields` contains pointers into the advertisement data for names/service data/manufacturer data rather than creating durable copies.

Extended advertising is a separate NimBLE GAP event/descriptor path in builds that enable BLE 5 extended advertising. B must branch on the actual event type rather than casting every discovery event to the legacy descriptor.

### Callback context

The GAP callback runs in the NimBLE host task/event context, not a hardware ISR. Keep it short for the same reason as other host callbacks.

### Buffer lifetime contract

Treat the `struct ble_gap_event *`, its discovery descriptor and advertisement data pointer as callback-scoped. If B consumes the advertisement later on another task, copy:

- native address value / address type;
- RSSI and other scalar descriptor metadata B needs;
- exactly `length_data` bytes of advertising data.

Do not queue `event`, `event->disc.data`, or pointers stored inside `ble_hs_adv_fields`.

Parsing the AD data synchronously inside the NimBLE host callback is legal if it remains small and nonblocking. Retaining the parsed pointer fields after callback return is not.

---

## 4. IEEE 802.15.4 promiscuous receive

### Entry point

Headers: `esp_ieee802154.h`, `esp_ieee802154_types.h`

Typical receive setup uses public APIs:

```c
esp_ieee802154_enable();
esp_ieee802154_set_channel(channel);       /* 11..26 */
esp_ieee802154_set_promiscuous(true);
esp_ieee802154_receive();
```

The radio delivers:

```c
void esp_ieee802154_receive_done(
    uint8_t *frame,
    esp_ieee802154_frame_info_t *frame_info);
```

Espressif explicitly documents IEEE 802.15.4 subsystem events in this section as **ISR context**.

### Native frame contract

The receive buffer begins with the PHY length byte followed by MHR and MAC payload; the driver validates FCS in hardware. The public documentation notes that the FCS bytes are not exposed as a normal FCS and that receive metadata includes RSSI/LQI information.

`esp_ieee802154_frame_info_t` is native metadata. Current public fields include receive RSSI, LQI, SFD timestamp and multipan match index; B should include the ESP-IDF type rather than mirror it.

### Mandatory ownership rule

After the upper layer is finished with the driver-owned `frame`, it **must** call:

```c
esp_ieee802154_receive_handle_done(frame);
```

The safest NearBy One ISR handoff is therefore:

1. read and validate `frame[0]` against the IEEE 802.15.4 maximum;
2. copy the length byte + frame bytes into a preallocated queue/ring slot;
3. copy `*frame_info` into that slot;
4. call `esp_ieee802154_receive_handle_done(frame)` **before returning from the ISR callback**;
5. wake a worker with `xQueueSendFromISR`/task notification;
6. B parses only the copied slot in task context.

This releases the scarce driver RX buffer immediately and avoids forcing the application worker to call a driver ownership API on an old pointer.

Do not:

- queue the original `frame` pointer;
- defer `receive_handle_done(frame)` until after parsing;
- allocate from heap in the ISR;
- parse Zigbee/Thread payloads in the ISR;
- log formatted frame dumps from the ISR.

If a future zero-copy experiment deliberately retains the driver frame, it must be separately measured for RX-buffer starvation and must preserve the exact `receive_handle_done()` ownership protocol. It is not the default path.

---

## 5. Scan-session / RF ownership contract

ESP32-C6 Wi-Fi, BLE and IEEE 802.15.4 share one 2.4 GHz RF subsystem. Coexistence support does not remove NearBy One's product-level requirement that a **complete discovery scan session is exclusive**.

The scan coordinator owns the scan gate once at session start and keeps it until all scheduled scanner modules are finished. Individual Wi-Fi/BLE/802.15.4 phases do not release/reacquire the product gate between phases.

While a scan session is active:

- UI-triggered device connect/read/write actions return `ESP_ERR_INVALID_STATE` / busy immediately;
- ad-hoc RF operations outside the coordinator return busy;
- Web management/provisioning must not silently start a competing scan;
- scanner phases may stop/start/reconfigure the radio stacks under the existing session ownership.

The lock protects product-level exclusivity, not every low-level radio callback. Never take this mutex in Wi-Fi callbacks, NimBLE callbacks or the IEEE 802.15.4 ISR.

---

## 6. Drop / overload policy

The first pass uses bounded preallocated queues/rings. When a producer outruns its worker:

- increment a per-transport drop counter;
- release any driver-owned buffer immediately as required;
- do not block a driver callback or ISR waiting for space;
- expose the drop count in measurement logs so B can distinguish parser misses from capture overload.

No cross-transport `packet_t` is introduced. Wi-Fi, NimBLE and 802.15.4 retain separate native-facing handoff records because their metadata, lifetime and callback rules are materially different.

## Upstream anchors

- Espressif `examples/network/simple_sniffer/main/cmd_sniffer.c` for Wi-Fi promiscuous startup/callback style.
- Espressif `components/esp_wifi/include/esp_wifi.h` and native Wi-Fi types for callback/structure definitions.
- Espressif NimBLE central/client examples (`examples/bluetooth/nimble/blecent`, `ble_spp/spp_client`) for `BLE_GAP_EVENT_DISC` and `ble_hs_adv_parse_fields()`.
- Espressif `components/ieee802154/include/esp_ieee802154.h` for ISR context and `receive_handle_done()` ownership.
- Espressif `examples/ieee802154/ieee802154_cli` for public radio setup/receive flow.
