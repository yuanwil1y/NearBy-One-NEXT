#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIRESHARK_BLE_UUID_STR_LEN 37
#define WIRESHARK_BLE_LOCAL_NAME_MAX 32
#define WIRESHARK_BLE_MAX_SERVICE_UUIDS 12
#define WIRESHARK_BLE_MAX_SERVICE_DATA 6
#define WIRESHARK_BLE_MAX_MANUFACTURER_DATA 4
#define WIRESHARK_BLE_MATCH_BYTES_MAX 24

typedef enum {
    WIRESHARK_BLE_PARSE_OK = 0,
    WIRESHARK_BLE_PARSE_PARTIAL = 1,
    WIRESHARK_BLE_PARSE_ERR_ARG = -1,
    WIRESHARK_BLE_PARSE_ERR_MALFORMED = -2,
} wireshark_ble_parse_result_t;

typedef struct {
    uint8_t bytes[16];
} wireshark_ble_uuid_t;

typedef struct {
    wireshark_ble_uuid_t uuid;
    uint8_t data[WIRESHARK_BLE_MATCH_BYTES_MAX];
    uint8_t data_len;
    bool data_truncated;
} wireshark_ble_service_data_t;

typedef struct {
    uint16_t company_id;
    uint8_t data[WIRESHARK_BLE_MATCH_BYTES_MAX];
    uint8_t data_len;
    bool data_truncated;
} wireshark_ble_manufacturer_data_t;

typedef struct {
    bool has_flags;
    uint8_t flags;

    bool has_local_name;
    bool local_name_complete;
    bool local_name_truncated;
    char local_name[WIRESHARK_BLE_LOCAL_NAME_MAX + 1];

    wireshark_ble_uuid_t service_uuids[WIRESHARK_BLE_MAX_SERVICE_UUIDS];
    uint8_t service_uuid_count;
    bool service_uuid_overflow;

    wireshark_ble_service_data_t service_data[WIRESHARK_BLE_MAX_SERVICE_DATA];
    uint8_t service_data_count;
    bool service_data_overflow;

    wireshark_ble_manufacturer_data_t manufacturer_data[WIRESHARK_BLE_MAX_MANUFACTURER_DATA];
    uint8_t manufacturer_data_count;
    bool manufacturer_data_overflow;

    bool has_tx_power;
    int8_t tx_power;
    bool has_appearance;
    uint16_t appearance;

    bool source_incomplete;
    bool partial_tail;
} wireshark_ble_discovery_facts_t;

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
} wireshark_ble_native_meta_t;

typedef struct {
    wireshark_ble_native_meta_t meta;
    wireshark_ble_discovery_facts_t facts;
} wireshark_ble_observation_t;

/* Phase 1 contracts; implementations remain under nearby_* until Phase 2. */
wireshark_ble_parse_result_t wireshark_ble_parse_adv(
    const uint8_t *data,
    size_t data_len,
    bool source_incomplete,
    wireshark_ble_discovery_facts_t *out);

void wireshark_ble_uuid_format(const wireshark_ble_uuid_t *uuid,
                               char out[WIRESHARK_BLE_UUID_STR_LEN]);

const wireshark_ble_manufacturer_data_t *wireshark_ble_find_manufacturer_data(
    const wireshark_ble_discovery_facts_t *facts,
    uint16_t company_id);
const wireshark_ble_service_data_t *wireshark_ble_find_service_data(
    const wireshark_ble_discovery_facts_t *facts,
    const wireshark_ble_uuid_t *uuid);
bool wireshark_ble_has_service_uuid(const wireshark_ble_discovery_facts_t *facts,
                                    const wireshark_ble_uuid_t *uuid);

#ifdef __cplusplus
}
#endif
