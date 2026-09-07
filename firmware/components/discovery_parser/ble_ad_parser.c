#include "nearby_ble_discovery.h"

nearby_ble_parse_result_t nearby_ble_parse_advertisement(
    const uint8_t *data,
    size_t data_len,
    bool source_incomplete,
    nearby_ble_discovery_facts_t *out)
{
    return wireshark_ble_parse_adv(data, data_len, source_incomplete, out);
}

void nearby_ble_uuid_format(const nearby_ble_uuid_t *uuid,
                            char out[NEARBY_BLE_UUID_STR_LEN])
{
    wireshark_ble_uuid_format(uuid, out);
}

const nearby_ble_manufacturer_data_t *nearby_ble_find_manufacturer_data(
    const nearby_ble_discovery_facts_t *facts,
    uint16_t company_id)
{
    return wireshark_ble_find_manufacturer_data(facts, company_id);
}

const nearby_ble_service_data_t *nearby_ble_find_service_data(
    const nearby_ble_discovery_facts_t *facts,
    const nearby_ble_uuid_t *uuid)
{
    return wireshark_ble_find_service_data(facts, uuid);
}

bool nearby_ble_has_service_uuid(const nearby_ble_discovery_facts_t *facts,
                                 const nearby_ble_uuid_t *uuid)
{
    return wireshark_ble_has_service_uuid(facts, uuid);
}
