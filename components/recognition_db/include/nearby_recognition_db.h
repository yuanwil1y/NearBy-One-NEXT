#pragma once

#include <stdbool.h>
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
#define NEARBY_DB_MATCH_WORKSPACE_RECOMMENDED 4096u
#define NEARBY_DB_MATCH_MAX_FACT_VALUES 32u

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

typedef struct {
    const char *ptr;
    size_t len;
} nearby_db_text_t;

typedef struct {
    nearby_db_text_t key;
    nearby_db_text_t value;
} nearby_db_kv_t;

typedef struct {
    uint16_t manufacturer_id;
    const uint8_t *data;
    size_t data_len;
} nearby_db_ble_manufacturer_data_t;

typedef struct {
    bool connectable;
    nearby_db_text_t local_name;
    const nearby_db_text_t *service_uuids;
    size_t service_uuid_count;
    const nearby_db_text_t *service_data_uuids;
    size_t service_data_uuid_count;
    const nearby_db_ble_manufacturer_data_t *manufacturer_data;
    size_t manufacturer_data_count;
} nearby_db_ble_facts_t;

typedef struct {
    nearby_db_text_t service_type;
    nearby_db_text_t name;
    const nearby_db_kv_t *properties;
    size_t property_count;
} nearby_db_zeroconf_facts_t;

typedef struct {
    const nearby_db_kv_t *fields;
    size_t field_count;
} nearby_db_ssdp_facts_t;

typedef struct {
    nearby_db_text_t hostname;
    nearby_db_text_t macaddress;
    bool registered_device;
} nearby_db_dhcp_facts_t;

typedef struct {
    void *data;
    size_t size;
} nearby_db_match_workspace_t;

typedef enum {
    NEARBY_DB_MATCH_NOT_FOUND = 0,
    NEARBY_DB_MATCHED = 1,
    NEARBY_DB_MATCH_AMBIGUOUS = 2,
} nearby_db_match_status_t;

typedef struct {
    nearby_db_match_status_t status;
    uint32_t matched_record_count;
    /* Valid only when status == NEARBY_DB_MATCHED. */
    nearby_db_value_ref_t value;
} nearby_db_match_result_t;

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

/**
 * Typed B -> C recognition queries for HA-derived BLE/LAN matchers.
 *
 * Agent B supplies already parsed/normalized observation facts only. Agent C
 * owns candidate iteration and matcher evaluation inside the SD-backed DB. The
 * caller supplies reusable scratch memory; no matcher table or candidate set is
 * loaded into RAM. Operational errors are returned as nearby_db_result_t;
 * MATCHED/AMBIGUOUS/NOT_FOUND is returned in out_result->status.
 */
nearby_db_result_t nearby_db_match_ble(
    nearby_db_t *db,
    const nearby_db_ble_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

nearby_db_result_t nearby_db_match_zeroconf(
    nearby_db_t *db,
    const nearby_db_zeroconf_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

nearby_db_result_t nearby_db_match_ssdp(
    nearby_db_t *db,
    const nearby_db_ssdp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

nearby_db_result_t nearby_db_match_dhcp(
    nearby_db_t *db,
    const nearby_db_dhcp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

#ifdef __cplusplus
}
#endif
