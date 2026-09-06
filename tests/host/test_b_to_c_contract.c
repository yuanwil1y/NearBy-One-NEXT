#include "nearby_recognition_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void uuid16(uint16_t value, nearby_ble_uuid_t *uuid)
{
    static const uint8_t base[16] = {
        0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,
        0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb,
    };
    memcpy(uuid->bytes, base, sizeof(uuid->bytes));
    uuid->bytes[2] = (uint8_t)(value >> 8);
    uuid->bytes[3] = (uint8_t)value;
}

static void expect_status(nearby_db_result_t rc,
                          const nearby_db_match_result_t *result,
                          nearby_db_match_status_t status,
                          bool usable)
{
    assert(rc == NEARBY_DB_OK);
    assert(result->status == status);
    assert(nearby_recognition_result_is_usable(result) == usable);
    if (status != NEARBY_DB_MATCHED) {
        assert(result->value.value_size == 0u);
    }
}

static void test_ble(nearby_db_t *db, nearby_db_match_workspace_t *workspace)
{
    nearby_ble_normalized_t observation;
    memset(&observation, 0, sizeof(observation));
    observation.meta.connectable_known = true;
    observation.meta.connectable = false;
    observation.facts.manufacturer_data_count = 1;
    observation.facts.manufacturer_data[0].company_id = 123;
    observation.facts.manufacturer_data[0].data_len = 3;
    observation.facts.manufacturer_data[0].data[0] = 1;
    observation.facts.manufacturer_data[0].data[1] = 2;
    observation.facts.manufacturer_data[0].data[2] = 9;

    nearby_db_match_result_t result;
    expect_status(nearby_recognition_match_ble(db, &observation, workspace, &result),
                  &result, NEARBY_DB_MATCHED, true);

    observation.facts.manufacturer_data[0].data[1] = 9;
    expect_status(nearby_recognition_match_ble(db, &observation, workspace, &result),
                  &result, NEARBY_DB_MATCH_NOT_FOUND, false);

    memset(&observation, 0, sizeof(observation));
    observation.meta.connectable_known = true;
    observation.meta.connectable = true;
    observation.facts.manufacturer_data_count = 1;
    observation.facts.manufacturer_data[0].company_id = 321;
    observation.facts.manufacturer_data[0].data_len = 1;
    observation.facts.manufacturer_data[0].data[0] = 7;
    observation.facts.service_uuid_count = 1;
    uuid16(0x1111, &observation.facts.service_uuids[0]);
    expect_status(nearby_recognition_match_ble(db, &observation, workspace, &result),
                  &result, NEARBY_DB_MATCHED, true);

    observation.facts.service_data_count = 1;
    uuid16(0x2222, &observation.facts.service_data[0].uuid);
    expect_status(nearby_recognition_match_ble(db, &observation, workspace, &result),
                  &result, NEARBY_DB_MATCH_AMBIGUOUS, false);
}

static void test_zeroconf(nearby_db_t *db, nearby_db_match_workspace_t *workspace)
{
    nearby_mdns_service_t service;
    memset(&service, 0, sizeof(service));
    strcpy(service.service_type, "_demo._tcp.local.");
    strcpy(service.instance, "demo-device._demo._tcp.local.");
    service.txt_count = 2;
    strcpy(service.txt[0].key, "model");
    strcpy(service.txt[0].value, "x100");
    service.txt[0].has_value = true;
    strcpy(service.txt[1].key, "id");
    strcpy(service.txt[1].value, "42");
    service.txt[1].has_value = true;

    nearby_db_match_result_t result;
    expect_status(nearby_recognition_match_zeroconf(db, &service, workspace, &result),
                  &result, NEARBY_DB_MATCHED, true);

    memset(&service, 0, sizeof(service));
    strcpy(service.service_type, "_amb._tcp.local.");
    strcpy(service.instance, "amb-device._amb._tcp.local.");
    service.txt_count = 1;
    strcpy(service.txt[0].key, "model");
    strcpy(service.txt[0].value, "amb100");
    service.txt[0].has_value = true;
    expect_status(nearby_recognition_match_zeroconf(db, &service, workspace, &result),
                  &result, NEARBY_DB_MATCH_AMBIGUOUS, false);
}

static void test_ssdp(nearby_db_t *db, nearby_db_match_workspace_t *workspace)
{
    nearby_ssdp_facts_t facts;
    memset(&facts, 0, sizeof(facts));
    strcpy(facts.st, "urn:demo:1");
    strcpy(facts.manufacturer, "Acme");
    nearby_db_match_result_t result;
    expect_status(nearby_recognition_match_ssdp(db, &facts, workspace, &result),
                  &result, NEARBY_DB_MATCHED, true);

    strcpy(facts.model_name, "Other");
    expect_status(nearby_recognition_match_ssdp(db, &facts, workspace, &result),
                  &result, NEARBY_DB_MATCH_AMBIGUOUS, false);
}

static void test_dhcp(nearby_db_t *db, nearby_db_match_workspace_t *workspace)
{
    nearby_dhcp_facts_t facts;
    memset(&facts, 0, sizeof(facts));
    strcpy(facts.hostname, "demo-one");
    facts.chaddr_len = 6;
    const uint8_t demo_mac[6] = {0xaa,0xbb,0xcc,0x11,0x22,0x33};
    memcpy(facts.chaddr, demo_mac, sizeof(demo_mac));
    nearby_db_match_result_t result;
    expect_status(nearby_recognition_match_dhcp(db, &facts, workspace, &result),
                  &result, NEARBY_DB_MATCHED, true);

    strcpy(facts.hostname, "amb-one");
    const uint8_t amb_mac[6] = {0xdd,0xee,0xff,0x11,0x22,0x33};
    memcpy(facts.chaddr, amb_mac, sizeof(amb_mac));
    expect_status(nearby_recognition_match_dhcp(db, &facts, workspace, &result),
                  &result, NEARBY_DB_MATCH_AMBIGUOUS, false);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    nearby_db_t db;
    assert(nearby_db_open(&db, argv[1]) == NEARBY_DB_OK);
    unsigned char scratch[NEARBY_DB_MATCH_WORKSPACE_RECOMMENDED];
    nearby_db_match_workspace_t workspace = {scratch, sizeof(scratch)};

    test_ble(&db, &workspace);
    test_zeroconf(&db, &workspace);
    test_ssdp(&db, &workspace);
    test_dhcp(&db, &workspace);

    unsigned char tiny[16];
    nearby_db_match_workspace_t tiny_workspace = {tiny, sizeof(tiny)};
    nearby_mdns_service_t zc;
    memset(&zc, 0, sizeof(zc));
    strcpy(zc.service_type, "_demo._tcp.local.");
    strcpy(zc.instance, "demo-device._demo._tcp.local.");
    zc.txt_count = 2;
    strcpy(zc.txt[0].key, "model"); strcpy(zc.txt[0].value, "x100"); zc.txt[0].has_value = true;
    strcpy(zc.txt[1].key, "id"); strcpy(zc.txt[1].value, "42"); zc.txt[1].has_value = true;
    nearby_db_match_result_t result;
    assert(nearby_recognition_match_zeroconf(&db, &zc, &tiny_workspace, &result) == NEARBY_DB_ERR_RANGE);

    nearby_db_close(&db);
    puts("B to frozen-C typed recognition contract: OK");
    return 0;
}
