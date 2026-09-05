# NearBy Recognition DB header and whole-SD install contract (v0)

## Product rule

Recognition DB import is **destructive for the entire SD card** in v0.1. Formatting/reinitialization deletes every existing SD file. Agent C defines only the recognition-file validation and success semantics; Agent D owns FAT/SD formatting and the upload/write loop, and Agent E owns the warning/progress UI.

There is no previous-generation preservation, generation pointer, rollback directory, or promise that an old DB remains available after formatting.

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
| 40 | 8 | `manifest_offset` | absolute offset; v0 must be `128` |
| 48 | 8 | `manifest_size` | manifest bytes |
| 56 | 8 | `payload_offset` | first byte after manifest |
| 64 | 8 | `payload_size` | bytes from payload offset to EOF |
| 72 | 4 | `payload_crc32` | CRC32 over payload bytes |
| 76 | 4 | `header_crc32` | CRC32 over the 128-byte header with this field zeroed |
| 80 | 32 | `payload_sha256` | SHA-256 over payload; zeros only when flag bit 0 is clear |
| 112 | 8 | `build_epoch` | Unix seconds; informational, not trusted for compatibility |
| 120 | 8 | `reserved` | must be zero in v0 |

The manifest is canonical UTF-8 JSON in the PoC and carries source provenance, pinned revisions, classification and redistribution state. Payload/index encoding can evolve behind the container contract while schema/reader compatibility remains explicit.

## Browser/local-source preflight — before format

The complete local `.nbdb` source MUST pass preflight before destructive formatting can be enabled. The browser may read it incrementally; it does not need to buffer the entire file.

Required checks:

1. source length is at least 128 bytes;
2. magic, format major, header size, schema version and reader ABI are compatible;
3. header CRC32 matches;
4. declared file size equals the local file size;
5. manifest/payload ranges are monotonic, non-overlapping and inside the file;
6. manifest parses and `source_count` matches;
7. every source has stable provenance, pinned revision, license, redistribution state, classification and transform metadata;
8. release-mode install rejects any source not explicitly `allowed`;
9. payload CRC32 matches after streaming the complete payload;
10. SHA-256 matches whenever the SHA flag is present/required.

The machine-facing preflight status is intentionally small:

```text
ok / safe_to_format
policy_mode: development | release
install_model: whole_sd_destructive_v0_1
destructive: true
active_path: /nearby/db/nearby.nbdb
temporary_path: /nearby/db/nearby.nbdb.part
post_format_failure: no_usable_db_guaranteed
errors[]
```

`safe_to_format` MUST be false for every truncated, corrupt, incompatible or policy-blocked source. A valid preflight is permission to *offer* the destructive action, not permission to format without explicit user confirmation.

## Install state machine

```text
SELECT_SOURCE
  -> PREFLIGHT_LOCAL_SOURCE
  -> WAIT_EXPLICIT_DESTRUCTIVE_CONFIRMATION
  -> FORMAT_WHOLE_SD                 # owned by D; irreversible
  -> CREATE /nearby/db/
  -> STREAM nearby.nbdb.part         # owned by D; bounded buffer
  -> VERIFY_RECEIVED_FILE
  -> RENAME nearby.nbdb.part -> nearby.nbdb
  -> ACTIVE
```

After `FORMAT_WHOLE_SD`, the old contents are gone. Cancellation, power loss, I/O failure, length mismatch, CRC/SHA failure, manifest failure, or reopen failure after that point may leave **no usable recognition DB**. Recovery is simply another import attempt; v0.1 has no generation manager or rollback promise.

The device may repeat cheap header/compatibility checks before accepting bytes and MUST compute stream length/integrity while D writes. Those checks are defense in depth; they do not change the destructive failure semantics.

## Post-format success criteria

Promotion to the active filename is permitted only when all of the following are true:

- transfer uses bounded RAM; target transfer/integrity scratch remains <= 16 KiB;
- received byte count equals header `file_size` exactly;
- payload CRC32 and SHA-256 equal the header values;
- manifest/source policy still passes the selected install mode;
- closed file can be reopened and the container/header/ranges revalidated;
- only then is `nearby.nbdb.part` renamed/promoted to `nearby.nbdb`.

If verification fails, `nearby.nbdb.part` is not an active DB. Agent C does not prescribe whether D deletes the failed `.part` immediately or on the next format/import.

## SD layout

Normal runtime:

```text
/nearby/db/
  nearby.nbdb
```

Only during an import after format:

```text
/nearby/db/
  nearby.nbdb.part
```

No `/current`, `gen-*`, rollback tree, runtime Device/Entity/State persistence, or user-file preservation is part of the v0.1 recognition DB contract.
