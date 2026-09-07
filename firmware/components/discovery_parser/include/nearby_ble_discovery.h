#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wireshark_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Wireshark owns BLE dissection types. */
#define NEARBY_BLE_UUID_STR_LEN WIRESHARK_BLE_UUID_STR_LEN
#define NEARBY_BLE_LOCAL_NAME_MAX WIRESHARK_BLE_LOCAL_NAME_MAX
#define NEARBY_BLE_MAX_SERVICE_UUIDS WIRESHARK_BLE_MAX_SERVICE_UUIDS
#define NEARBY_BLE_MAX_SERVICE_DATA WIRESHARK_BLE_MAX_SERVICE_DATA
#define NEARBY_BLE_MAX_MANUFACTURER_DATA WIRESHARK_BLE_MAX_MANUFACTURER_DATA
#define NEARBY_BLE_MATCH_BYTES_MAX WIRESHARK_BLE_MATCH_BYTES_MAX

typedef wireshark_ble_parse_result_t nearby_ble_parse_result_t;
#define NEARBY_BLE_PARSE_OK WIRESHARK_BLE_PARSE_OK
#define NEARBY_BLE_PARSE_PARTIAL WIRESHARK_BLE_PARSE_PARTIAL
#define NEARBY_BLE_PARSE_ERR_ARG WIRESHARK_BLE_PARSE_ERR_ARG
#define NEARBY_BLE_PARSE_ERR_MALFORMED WIRESHARK_BLE_PARSE_ERR_MALFORMED

typedef wireshark_ble_uuid_t nearby_ble_uuid_t;
typedef wireshark_ble_service_data_t nearby_ble_service_data_t;
typedef wireshark_ble_manufacturer_data_t nearby_ble_manufacturer_data_t;
typedef wireshark_ble_discovery_facts_t nearby_ble_discovery_facts_t;
typedef wireshark_ble_native_meta_t nearby_ble_native_meta_t;
typedef wireshark_ble_observation_t nearby_ble_normalized_t;

/* Legacy symbols retained until the Phase 2 implementation migration. */
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
