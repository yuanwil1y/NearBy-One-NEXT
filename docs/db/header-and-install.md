# NearBy Recognition DB header and pre-format validation contract (v0)

## Goal

A browser or ESP32-C6 must be able to reject an incompatible/corrupt database **before SD formatting is started**. Browser validation runs against the local upload file; the device repeats validation before replacing the active DB.

## File header

All integer fields are little-endian. Header size is fixed at 128 bytes for schema v0.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII bytes `NBYDB\0\r\n` |
| 8 | 2 | `format_major` | `1` for this container format |
| 10 | 2 | `format_minor` | `0` initially; additive reader-compatible changes may increment |
| 12 | 2 | `header_size` | exactly `128` |
| 14 | 2 | `flags` | bit 0 = payload SHA-256 present; unknown mandatory bits reject |
| 16 | 4 | `schema_version` | normalized record schema; currently `1` |
| 20 | 4 | `db_version` | monotonically increasing build/version integer |
| 24 | 4 | `min_reader_abi` | minimum firmware DB reader ABI |
| 28 | 4 | `source_count` | number of source records in manifest |
| 32 | 8 | `file_size` | exact total bytes of the `.nbdb` file |
| 40 | 8 | `manifest_offset` | absolute offset; v0 normally `128` |
| 48 | 8 | `manifest_size` | manifest bytes |
| 56 | 8 | `payload_offset` | first byte after manifest |
| 64 | 8 | `payload_size` | bytes from payload offset to EOF |
| 72 | 4 | `payload_crc32` | CRC32 over payload bytes |
| 76 | 4 | `header_crc32` | CRC32 over the 128-byte header with this field zeroed |
| 80 | 32 | `payload_sha256` | SHA-256 over payload; zeros only when flag bit 0 is clear |
| 112 | 8 | `build_epoch` | Unix seconds; informational, not trusted for compatibility |
| 120 | 8 | `reserved` | must be zero in v0 |

The manifest is canonical UTF-8 JSON in the PoC. It records source provenance, shard directory entries, build tool version, and licensing state. Final payload shards can change encoding without changing the pre-format validation sequence as long as the header/container contract remains compatible.

## Browser validation before destructive formatting

Given a user-selected `.nbdb` file, the Web Portal MUST perform all of these checks before it enables or calls SD format:

1. File length is at least 128 bytes and equals `file_size`.
2. `magic`, `format_major`, `header_size`, `schema_version`, and `min_reader_abi` are supported by the target firmware.
3. Header CRC32 matches after zeroing `header_crc32`.
4. Manifest and payload ranges are non-overlapping, monotonic, inside `file_size`, and `payload_offset + payload_size == file_size`.
5. Manifest JSON parses, its source count equals `source_count`, and every source has provenance/license/redistribution fields.
6. No manifest source has `redistribution` = `review-required` or `reference-only` for a release-mode install unless the release policy explicitly allows it.
7. Stream through the local file once to verify `payload_crc32` and SHA-256. This can be done in the browser without touching the SD card.
8. Only after all checks pass may the UI offer the destructive format/install action.

A bad header therefore cannot trigger SD formatting. Payload integrity also gets checked before format because the source file is already locally readable by the browser.

## Device-side streaming install

The device repeats the cheap header checks before writing. The upload/install path writes to `/nearby/db/.staging/nearby.nbdb`, computes CRC32/SHA-256 while streaming, fsyncs/closes, reopens and validates the final file, then atomically promotes the staging generation. The previous generation remains active until promotion succeeds.

Success criteria:

- no full DB or full shard is buffered in RAM;
- bounded transfer buffer target: <= 16 KiB;
- received byte count exactly equals `file_size`;
- computed payload CRC32 and SHA-256 match the header;
- manifest passes compatibility/license policy checks;
- post-write reopen succeeds and header revalidates;
- only then update `/nearby/db/current` (or equivalent generation pointer).

Power loss or upload cancellation before promotion leaves the previous generation usable and the staging generation disposable.

## Proposed SD layout

```text
/nearby/db/
  current                 # tiny generation pointer / metadata
  gen-00000042/
    nearby.nbdb           # v0 container; later may become header+manifest + shards
  .staging/
    nearby.nbdb.part
```

The logical payload directory in the manifest is protocol-sharded (`bluetooth`, `zigbee`, `lan`, `matter`, `vendors`) with sorted indexes and a deduplicated string table. Runtime session Device/Entity/State data is never stored here.
