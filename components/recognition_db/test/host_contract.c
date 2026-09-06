#include "nearby_recognition_db.h"

#include <stdio.h>
#include <string.h>

#define TXT(lit) ((nearby_db_text_t){(lit), sizeof(lit) - 1u})

static int expect_query(
    const char *label,
    nearby_db_result_t rc,
    const nearby_db_match_result_t *result,
    nearby_db_match_status_t expected_status,
    uint32_t minimum_records) {
    if (rc != NEARBY_DB_OK) {
        fprintf(stderr, "%s: rc=%d\n", label, (int)rc);
        return 1;
    }
    if (result->status != expected_status) {
        fprintf(stderr, "%s: status=%d expected=%d\n", label, (int)result->status, (int)expected_status);
        return 1;
    }
    if (result->matched_record_count < minimum_records) {
        fprintf(stderr, "%s: matched_record_count=%u expected >=%u\n",
                label, (unsigned)result->matched_record_count, (unsigned)minimum_records);
        return 1;
    }
    if (expected_status == NEARBY_DB_MATCHED && result->value.value_size == 0u) {
        fprintf(stderr, "%s: matched result missing value ref\n", label);
        return 1;
    }
    if (expected_status != NEARBY_DB_MATCHED && result->value.value_size != 0u) {
        fprintf(stderr, "%s: non-unique result leaked value ref\n", label);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s DB\n", argv[0]);
        return 2;
    }

    nearby_db_t db;
    if (nearby_db_open(&db, argv[1]) != NEARBY_DB_OK) {
        fprintf(stderr, "open failed\n");
        return 3;
    }

    unsigned char scratch_data[NEARBY_DB_MATCH_WORKSPACE_RECOMMENDED];
    nearby_db_match_workspace_t workspace = {scratch_data, sizeof(scratch_data)};
    nearby_db_match_result_t result;
    int failed = 0;

    static const uint8_t mfr123_data[] = {1, 2, 9};
    nearby_db_ble_manufacturer_data_t mfr123[] = {{123, mfr123_data, sizeof(mfr123_data)}};
    nearby_db_ble_facts_t ble123 = {
        .connectable = false,
        .manufacturer_data = mfr123,
        .manufacturer_data_count = 1,
    };
    failed |= expect_query(
        "ble manufacturer prefix",
        nearby_db_match_ble(&db, &ble123, &workspace, &result),
        &result,
        NEARBY_DB_MATCHED,
        1);

    static const uint8_t wrong_mfr123_data[] = {1, 9};
    mfr123[0].data = wrong_mfr123_data;
    mfr123[0].data_len = sizeof(wrong_mfr123_data);
    failed |= expect_query(
        "ble manufacturer data mismatch",
        nearby_db_match_ble(&db, &ble123, &workspace, &result),
        &result,
        NEARBY_DB_MATCH_NOT_FOUND,
        0);

    nearby_db_ble_facts_t ble_name = {
        .connectable = true,
        .local_name = TXT("Demo-Unit"),
    };
    failed |= expect_query(
        "ble local name wildcard",
        nearby_db_match_ble(&db, &ble_name, &workspace, &result),
        &result,
        NEARBY_DB_MATCHED,
        1);

    static const nearby_db_text_t same_service[] = {
        TXT("00001111-0000-1000-8000-00805f9b34fb"),
    };
    static const uint8_t mfr321_data[] = {7};
    nearby_db_ble_manufacturer_data_t mfr321[] = {{321, mfr321_data, sizeof(mfr321_data)}};
    nearby_db_ble_facts_t ble_same_domain = {
        .connectable = true,
        .service_uuids = same_service,
        .service_uuid_count = 1,
        .manufacturer_data = mfr321,
        .manufacturer_data_count = 1,
    };
    failed |= expect_query(
        "ble duplicate matcher same domain",
        nearby_db_match_ble(&db, &ble_same_domain, &workspace, &result),
        &result,
        NEARBY_DB_MATCHED,
        2);

    static const nearby_db_text_t other_service_data[] = {
        TXT("00002222-0000-1000-8000-00805f9b34fb"),
    };
    ble_same_domain.service_data_uuids = other_service_data;
    ble_same_domain.service_data_uuid_count = 1;
    failed |= expect_query(
        "ble multiple domains fail closed",
        nearby_db_match_ble(&db, &ble_same_domain, &workspace, &result),
        &result,
        NEARBY_DB_MATCH_AMBIGUOUS,
        3);

    nearby_db_kv_t zc_props[] = {
        {TXT("model"), TXT("x100")},
        {TXT("id"), TXT("42")},
    };
    nearby_db_zeroconf_facts_t zc = {
        .service_type = TXT("_demo._tcp.local."),
        .name = TXT("demo-device._demo._tcp.local."),
        .properties = zc_props,
        .property_count = 2,
    };
    failed |= expect_query(
        "zeroconf typed facts",
        nearby_db_match_zeroconf(&db, &zc, &workspace, &result),
        &result,
        NEARBY_DB_MATCHED,
        1);
    zc_props[1].value = TXT("wrong");
    failed |= expect_query(
        "zeroconf property mismatch",
        nearby_db_match_zeroconf(&db, &zc, &workspace, &result),
        &result,
        NEARBY_DB_MATCH_NOT_FOUND,
        0);

    nearby_db_kv_t ssdp_fields[] = {
        {TXT("ST"), TXT("urn:demo:1")},
        {TXT("manufacturer"), TXT("Acme")},
        {TXT("modelName"), TXT("Other")},
    };
    nearby_db_ssdp_facts_t ssdp = {
        .fields = ssdp_fields,
        .field_count = 2,
    };
    failed |= expect_query(
        "ssdp subset matcher",
        nearby_db_match_ssdp(&db, &ssdp, &workspace, &result),
        &result,
        NEARBY_DB_MATCHED,
        1);
    ssdp.field_count = 3;
    failed |= expect_query(
        "ssdp multiple domains fail closed",
        nearby_db_match_ssdp(&db, &ssdp, &workspace, &result),
        &result,
        NEARBY_DB_MATCH_AMBIGUOUS,
        2);
    ssdp_fields[1].value = TXT("Wrong");
    ssdp.field_count = 2;
    failed |= expect_query(
        "ssdp second field mismatch",
        nearby_db_match_ssdp(&db, &ssdp, &workspace, &result),
        &result,
        NEARBY_DB_MATCH_NOT_FOUND,
        0);

    nearby_db_dhcp_facts_t dhcp = {
        .hostname = TXT("demo-one"),
        .macaddress = TXT("AABBCC112233"),
        .registered_device = false,
    };
    failed |= expect_query(
        "dhcp oui plus hostname",
        nearby_db_match_dhcp(&db, &dhcp, &workspace, &result),
        &result,
        NEARBY_DB_MATCHED,
        1);
    dhcp.hostname = TXT("wrong-one");
    failed |= expect_query(
        "dhcp hostname mismatch",
        nearby_db_match_dhcp(&db, &dhcp, &workspace, &result),
        &result,
        NEARBY_DB_MATCH_NOT_FOUND,
        0);
    dhcp.hostname = TXT("other-one");
    dhcp.macaddress = TXT("DDEEFF112233");
    failed |= expect_query(
        "dhcp hostname bucket",
        nearby_db_match_dhcp(&db, &dhcp, &workspace, &result),
        &result,
        NEARBY_DB_MATCHED,
        1);

    unsigned char tiny_scratch[16];
    nearby_db_match_workspace_t tiny_workspace = {tiny_scratch, sizeof(tiny_scratch)};
    zc_props[1].value = TXT("42");
    nearby_db_result_t tiny_rc = nearby_db_match_zeroconf(&db, &zc, &tiny_workspace, &result);
    if (tiny_rc != NEARBY_DB_ERR_RANGE) {
        fprintf(stderr, "bounded workspace: rc=%d expected=%d\n", (int)tiny_rc, (int)NEARBY_DB_ERR_RANGE);
        failed = 1;
    }

    nearby_db_close(&db);
    if (failed) {
        return 4;
    }
    puts("runtime recognition contract OK");
    return 0;
}
