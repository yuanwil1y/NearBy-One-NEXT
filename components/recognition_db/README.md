# recognition_db component

Minimal read-only ESP-IDF-facing reader for the Agent C `.nbdb` PoC format.

Properties:

- validates magic/schema/reader ABI/header CRC and container ranges at open;
- validates the flat payload header/index bounds;
- binary-searches 16-byte index entries with a 64-byte comparison scratch buffer;
- returns a `(value_offset, value_size)` reference and supports chunked reads;
- never loads the full DB, full index, shard, or value into RAM;
- does not parse HA/ZHA data or execute vendor code.

Full payload CRC32/SHA-256 verification remains an install-time responsibility before a staged DB generation is promoted. Runtime `open()` intentionally stays cheap.
