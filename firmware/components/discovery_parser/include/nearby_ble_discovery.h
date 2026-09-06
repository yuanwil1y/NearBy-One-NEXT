#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_BLE_UUID_STR_LEN 37
#define NEARBY_BLE_LOCAL_NAME_MAX 32
#define NEARBY_BLE_MAX_SERVICE_UUIDS 12
#define NEARBY_BLE_MAX_SERVICE_DATA 6
#define NEARBY_BLE_MAX_MANUFACTURER_DATA 4
#define NEARBY_BLE_MATCH_BYTES_MAX 24

typedef enum {
    NEARBY_BLE_PARSE_OK = 0,
    NEARBY_BLE_PARSE_PARTIAL = 1,
    NEARBY_BLE_PARSE_ERR_ARG = -1,
    NEARBY_BLE_PARSE_ERR_MALFORMED = -2,
} nearby_ble_parse_result_t;

typedef struct {
    uint8_t bytes[16];
} nearby_ble_uuid_t;

typedef struct {
    nearby_ble_uuid_t uuid;
    uint8_t data[NEARBY_BLE_MATCH_BYTES_MAX];
    uint8_t data_len;
    bool data_truncated;
} nearby_ble_service_data_t;

typedef struct {
    uint16_t company_id;
    uint8_t data[NEARBY_BLE_MATCH_BYTES_MAX];
    uint8_t data_len;
    bool data_truncated;
} nearby_ble_manufacturer_data_t;

/*
 * HA-compatible Bluetooth discovery facts, represented in bounded C storage.
 * Semantics intentionally mirror the fields HA Bluetooth matchers consume:
 * local_name, service_uuids, service_data, manufacturer_data, connectable.
 * This is discovery normalization only; it does not decide product identity.
 */
typedef struct {
    bool has_flags;
    uint8_t flags;

    bool has_local_name;
    bool local_name_complete;
    bool local_name_truncated;
    char local_name[NEARBY_BLE_LOCAL_NAME_MAX + 1];

    nearby_ble_uuid_t service_uuids[NEARBY_BLE_MAX_SERVICE_UUIDS];
    uint8_t service_uuid_count;
    bool service_uuid_overflow;

    nearby_ble_service_data_t service_data[NEARBY_BLE_MAX_SERVICE_DATA];
    uint8_t service_data_count;
    bool service_data_overflow;

    nearby_ble_manufacturer_data_t manufacturer_data[NEARBY_BLE_MAX_MANUFACTURER_DATA];
    uint8_t manufacturer_data_count;
    bool manufacturer_data_overflow;

    bool has_tx_power;
    int8_t tx_power;
    bool has_appearance;
    uint16_t appearance;

    bool source_incomplete;
    bool partial_tail;
} nearby_ble_discovery_facts_t;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    bool legacy;
    bool connectable_known;
    bool connectable;
    bool source_truncated;
    uint8_t data_status;
    uint8_t sid;
    uint8_t primary_phy;
    uint8_t secondary_phy;
    int8_t controller_tx_power;
    uint16_t periodic_adv_interval;
} nearby_ble_native_meta_t;

typedef struct {
    nearby_ble_native_meta_t meta;
    nearby_ble_discovery_facts_t facts;
} nearby_ble_normalized_t;

nearby_ble_parse_result_t nearby_ble_parse_advertisement(
    const uint8_t *data,
    size_t data_len,
    bool source_incomplete,
    nearby_ble_discovery_facts_t *out);

void nearby_ble_uuid_format(const nearby_ble_uuid_t *uuid,
                            char out[NEARBY_BLE_UUID_STR_LEN]);

const nearby_ble_manufacturer_data_t *nearby_ble_find_manufacturer_data(
    const nearby_ble_discovery_facts_t *facts,
    uint16_t company_id);
const nearby_ble_service_data_t *nearby_ble_find_service_data(
    const nearby_ble_discovery_facts_t *facts,
    const nearby_ble_uuid_t *uuid);
bool nearby_ble_has_service_uuid(const nearby_ble_discovery_facts_t *facts,
                                 const nearby_ble_uuid_t *uuid);

#ifdef __cplusplus
}
#endif
