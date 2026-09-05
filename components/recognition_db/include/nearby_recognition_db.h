#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_DB_OK = 0,
    /* A key exists but has multiple unresolved claims. out_ref is valid so the
     * caller may inspect candidates/provenance, but this is NOT a confirmed match. */
    NEARBY_DB_AMBIGUOUS = 1,
    NEARBY_DB_ERR_ARG = -1,
    NEARBY_DB_ERR_IO = -2,
    NEARBY_DB_ERR_FORMAT = -3,
    NEARBY_DB_ERR_UNSUPPORTED = -4,
    NEARBY_DB_ERR_NOT_FOUND = -5,
    NEARBY_DB_ERR_RANGE = -6,
} nearby_db_result_t;

#define NEARBY_DB_VALUE_FLAG_AMBIGUOUS 0x0001u

typedef struct {
    FILE *fp;
    uint32_t db_version;
    uint32_t schema_version;
    uint64_t file_size;
    uint64_t payload_offset;
    uint64_t payload_size;
    uint32_t record_count;
    uint64_t index_offset;
    uint64_t blob_offset;
} nearby_db_t;

typedef struct {
    uint64_t value_offset;
    uint32_t value_size;
    uint16_t flags;
} nearby_db_value_ref_t;

/** Open a .nbdb file and validate the fixed header + flat payload header.
 *  This is a cheap runtime-open check; full payload CRC/SHA validation belongs
 *  to the install path before the generation is promoted.
 */
nearby_db_result_t nearby_db_open(nearby_db_t *db, const char *path);

void nearby_db_close(nearby_db_t *db);

/** Binary-search a UTF-8 key without loading the DB or a shard into RAM.
 *
 * Returns NEARBY_DB_OK only for an unambiguous recognition record.
 * Returns NEARBY_DB_AMBIGUOUS for an unresolved collided key; out_ref is still
 * valid and points at a recognition_ambiguity value containing all candidates
 * and provenance. Callers MUST NOT treat AMBIGUOUS as a product match.
 */
nearby_db_result_t nearby_db_find(
    nearby_db_t *db,
    const char *key,
    size_t key_len,
    nearby_db_value_ref_t *out_ref);

/** Read a slice of a matched or ambiguous value. Returns bytes read in out_read. */
nearby_db_result_t nearby_db_read_value(
    nearby_db_t *db,
    const nearby_db_value_ref_t *ref,
    uint32_t value_offset,
    void *buf,
    size_t buf_size,
    size_t *out_read);

#ifdef __cplusplus
}
#endif
