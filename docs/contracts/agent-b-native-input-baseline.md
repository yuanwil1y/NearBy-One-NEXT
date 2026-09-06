# Agent B native-input baseline

Agent B implementation on `agent-b/scanner-projects-audit` is pinned to Agent D frozen commit:

```text
accdf91441d0abaf6a5d69aac31394465ab49b06
```

Do not silently support a different private D contract. Update this document and the B adapter explicitly if D changes.

## Public D headers consumed

```text
firmware/components/native_handoff/include/native_handoff.h
firmware/components/radio_runtime/include/radio_runtime.h
firmware/components/scan_session/include/scan_session.h
```

Frozen BLE contract:

```text
nearby_ble_disc_record_t
  struct ble_gap_disc_desc desc
  uint16_t original_length_data
  bool truncated
  uint8_t data[64]

nearby_ble_ext_disc_record_t
  struct ble_gap_ext_disc_desc desc
  uint16_t original_length_data
  bool truncated
  uint8_t data[255]
```

D repoints `desc.data` to durable slot storage. B may consume the record only until the matching release call. Legacy and extended records remain separate native paths.

Extended metadata preserved by D includes address, RSSI, TX power, SID, primary/secondary PHY, periodic advertising interval, properties and `data_status`. D does not reassemble chained reports. B therefore treats any non-`COMPLETE` data status as an incomplete source and never presents it as a complete advertisement.

## B normalized Bluetooth facts

B currently emits bounded typed facts matching Home Assistant Bluetooth matcher semantics:

```text
local_name
service_uuids
service_data[uuid] -> bounded bytes
manufacturer_data[company_id] -> bounded bytes
connectable when native metadata can prove it
RSSI/address-type/SID/PHY/data-status metadata
```

The pinned Home Assistant discovery source used by Agent C is:

```text
home-assistant/core
2a1c7d1a864ed7cf9f6fb16c8a0ece7d01e16d48
homeassistant/generated/bluetooth.py
```

The generated matcher fields include `manufacturer_id`, `service_uuid`, `service_data_uuid`, `local_name`, `connectable`, and in some records byte-prefix constraints such as manufacturer-data starts.

## C boundary

Agent C owns recognition and the runtime DB key/index design. Its current flat DB is explicitly provisional, and its demonstration Bluetooth record key contains C-side matcher metadata such as `domain` and record index. B must not attempt to synthesize those database-record keys.

B therefore exposes typed matcher inputs. C is responsible for indexing/searching its stored matchers and returning `MATCHED`, `AMBIGUOUS`, `NOT_FOUND`, or error state.

## Host validation

The pure AD parser is intentionally independent of ESP-IDF and can be compiled with:

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Ifirmware/components/discovery_parser/include \
  firmware/components/discovery_parser/ble_ad_parser.c \
  tests/host/test_ble_discovery.c \
  -o /tmp/test_ble_discovery
/tmp/test_ble_discovery
```

The D-native adapter is compiled only when the frozen `native_handoff` component is present on an integration branch.
