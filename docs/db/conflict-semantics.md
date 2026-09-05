# Recognition conflict semantics

Unresolved recognition collisions are **not successful product matches**.

A normalized key may be emitted by multiple upstream records that disagree about product/model/profile metadata. Canonical JSON lexical ordering is used only so builds are reproducible; it is never semantic precedence and must never select a product identity by itself.

## Generator contract

For one key:

- byte-identical duplicate claims are deduplicated;
- one remaining distinct claim is emitted as a normal recognition record;
- two or more distinct claims are emitted as one `recognition_ambiguity` envelope;
- the envelope keeps every distinct candidate claim, source ID, source revision and source path already present in those records;
- the build manifest keeps the same candidates in `conflict_ledger` with `resolution=unresolved_ambiguous`, `runtime_result=ambiguous`, and `manual_review=true`;
- there is no `winner` or `winner_reason` for an unresolved collision.

The candidate list is sorted by canonical JSON solely for deterministic build output.

## Runtime contract

Flat index flag `0x0001` means the key is ambiguous. The value referenced by that entry is still readable and contains the complete ambiguity envelope.

`nearby_db_find()` returns:

- `NEARBY_DB_OK` only for an unambiguous record;
- `NEARBY_DB_AMBIGUOUS` when the exact key exists but has unresolved candidates; `out_ref` is valid so provenance/candidates can be inspected;
- `NEARBY_DB_ERR_NOT_FOUND` when the key does not exist;
- other negative errors for corrupt/unsupported/I/O cases.

Callers must treat `NEARBY_DB_AMBIGUOUS` as fail-closed for product recognition. They may display/log candidates or use a later explicitly defined stronger matcher key, but they must not silently choose candidate 0.

Unknown nonzero flat-index flags are rejected as unsupported by the bounded C reader.

## Boundary

This mechanism does not resolve protocol semantics and does not add parsers. Agent B remains responsible for producing normalized matcher keys. Agent C only records that a supplied key is insufficient to determine one recognition claim.
