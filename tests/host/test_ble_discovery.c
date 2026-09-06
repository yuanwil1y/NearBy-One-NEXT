#include "nearby_ble_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_legacy_happy_path(void)
{
    const uint8_t adv[] = {
        2, 0x01, 0x06,
        5, 0x09, 'T', 'e', 's', 't',
        3, 0x03, 0x1c, 0x18,
        6, 0x16, 0x1c, 0x18, 0x01, 0x02, 0x03,
        6, 0xff, 0x4c, 0x00, 0xaa, 0xbb, 0xcc,
        2, 0x0a, 0xf4,
        3, 0x19, 0x40, 0x03,
    };
    nearby_ble_discovery_facts_t facts;
    assert(nearby_ble_parse_advertisement(adv, sizeof(adv), false, &facts) == NEARBY_BLE_PARSE_OK);
    assert(facts.has_flags && facts.flags == 0x06);
    assert(facts.has_local_name && facts.local_name_complete);
    assert(strcmp(facts.local_name, "Test") == 0);
    assert(facts.service_uuid_count == 1);
    assert(facts.service_data_count == 1);
    assert(facts.manufacturer_data_count == 1);
    assert(facts.manufacturer_data[0].company_id == 0x004c);
    assert(facts.manufacturer_data[0].data_len == 3);
    assert(facts.has_tx_power && facts.tx_power == -12);
    assert(facts.has_appearance && facts.appearance == 0x0340);

    char uuid[NEARBY_BLE_UUID_STR_LEN];
    nearby_ble_uuid_format(&facts.service_uuids[0], uuid);
    assert(strcmp(uuid, "0000181c-0000-1000-8000-00805f9b34fb") == 0);
    assert(nearby_ble_find_manufacturer_data(&facts, 0x004c) != NULL);
    assert(nearby_ble_find_service_data(&facts, &facts.service_uuids[0]) != NULL);
}

static void test_complete_name_wins(void)
{
    const uint8_t adv[] = {
        4, 0x08, 'A', 'B', 'C',
        6, 0x09, 'A', 'B', 'C', 'D', 'E',
        3, 0x08, 'X', 'Y',
    };
    nearby_ble_discovery_facts_t facts;
    assert(nearby_ble_parse_advertisement(adv, sizeof(adv), false, &facts) == NEARBY_BLE_PARSE_OK);
    assert(strcmp(facts.local_name, "ABCDE") == 0);
    assert(facts.local_name_complete);
}

static void test_partial_tail_only_when_source_incomplete(void)
{
    const uint8_t cut[] = {5, 0x09, 'A', 'B'};
    nearby_ble_discovery_facts_t facts;
    assert(nearby_ble_parse_advertisement(cut, sizeof(cut), true, &facts) == NEARBY_BLE_PARSE_PARTIAL);
    assert(facts.partial_tail);
    assert(nearby_ble_parse_advertisement(cut, sizeof(cut), false, &facts) == NEARBY_BLE_PARSE_ERR_MALFORMED);
}

static void test_malformed_uuid_width(void)
{
    const uint8_t adv[] = {4, 0x03, 0x0d, 0x18, 0xff};
    nearby_ble_discovery_facts_t facts;
    assert(nearby_ble_parse_advertisement(adv, sizeof(adv), false, &facts) == NEARBY_BLE_PARSE_ERR_MALFORMED);
}

static void test_128_bit_uuid_wire_order(void)
{
    const uint8_t adv[] = {
        17, 0x07,
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0xd2, 0xfc, 0x00, 0x00,
    };
    nearby_ble_discovery_facts_t facts;
    assert(nearby_ble_parse_advertisement(adv, sizeof(adv), false, &facts) == NEARBY_BLE_PARSE_OK);
    char uuid[NEARBY_BLE_UUID_STR_LEN];
    nearby_ble_uuid_format(&facts.service_uuids[0], uuid);
    assert(strcmp(uuid, "0000fcd2-0000-1000-8000-00805f9b34fb") == 0);
}

static void test_bounded_value_copy(void)
{
    uint8_t adv[44];
    adv[0] = 43;
    adv[1] = 0xff;
    adv[2] = 0x34;
    adv[3] = 0x12;
    for (size_t i = 4; i < sizeof(adv); ++i) {
        adv[i] = (uint8_t)i;
    }
    nearby_ble_discovery_facts_t facts;
    assert(nearby_ble_parse_advertisement(adv, sizeof(adv), false, &facts) == NEARBY_BLE_PARSE_OK);
    assert(facts.manufacturer_data[0].data_len == NEARBY_BLE_MATCH_BYTES_MAX);
    assert(facts.manufacturer_data[0].data_truncated);
}

int main(void)
{
    test_legacy_happy_path();
    test_complete_name_wins();
    test_partial_tail_only_when_source_incomplete();
    test_malformed_uuid_width();
    test_128_bit_uuid_wire_order();
    test_bounded_value_copy();
    puts("ble discovery host tests: OK");
    return 0;
}
