#include "nearby_matcher_keys.h"
#include "nearby_transport_dedup.h"
#include "recognition_contract.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_exact_c_keys(void)
{
    char key[NEARBY_MATCHER_KEY_MAX];
    const uint8_t mac[6] = {0xa1, 0xb2, 0xc3, 0x44, 0x55, 0x66};

    assert(nearby_match_key_zha_manufacturer_model("Acme", "Model-1", key, sizeof(key)) == NEARBY_MATCHER_KEY_OK);
    assert(strcmp(key, C_KEY_ZHA_EXAMPLE) == 0);

    assert(nearby_match_key_z2m_model("TS0601", key, sizeof(key)) == NEARBY_MATCHER_KEY_OK);
    assert(strcmp(key, C_KEY_Z2M_MODEL_EXAMPLE) == 0);

    assert(nearby_match_key_z2m_fingerprint("_TZE200_test", "TS0601", key, sizeof(key)) == NEARBY_MATCHER_KEY_OK);
    assert(strcmp(key, C_KEY_Z2M_FINGERPRINT_EXAMPLE) == 0);

    assert(nearby_match_key_matter_vidpid(0x1234, 0x00ab, key, sizeof(key)) == NEARBY_MATCHER_KEY_OK);
    assert(strcmp(key, C_KEY_MATTER_EXAMPLE) == 0);

    assert(nearby_match_key_oui24(mac, key, sizeof(key)) == NEARBY_MATCHER_KEY_OK);
    assert(strcmp(key, C_KEY_OUI_EXAMPLE) == 0);
}

static int preserve_c_lookup_result(int c_result)
{
    /* B must never turn AMBIGUOUS into OK or select a candidate. */
    return c_result;
}

static void test_ambiguity_fixture(void)
{
    assert(C_DB_AMBIGUOUS == 1);
    assert(C_DB_VALUE_FLAG_AMBIGUOUS == 0x0001u);
    assert(preserve_c_lookup_result(C_DB_OK) == C_DB_OK);
    assert(preserve_c_lookup_result(C_DB_AMBIGUOUS) == C_DB_AMBIGUOUS);
    assert(preserve_c_lookup_result(C_DB_ERR_NOT_FOUND) == C_DB_ERR_NOT_FOUND);
}

static nearby_ble_normalized_t sample_ble(void)
{
    nearby_ble_normalized_t obs;
    memset(&obs, 0, sizeof(obs));
    const uint8_t addr[6] = {1, 2, 3, 4, 5, 6};
    memcpy(obs.meta.address, addr, sizeof(addr));
    obs.meta.address_type = 1;
    obs.meta.rssi = -60;
    obs.meta.legacy = true;
    obs.meta.connectable_known = true;
    obs.meta.connectable = false;
    obs.meta.sid = 0xff;
    obs.facts.has_local_name = true;
    strcpy(obs.facts.local_name, "sensor");
    obs.facts.local_name_complete = true;
    obs.facts.manufacturer_data_count = 1;
    obs.facts.manufacturer_data[0].company_id = 0x004c;
    obs.facts.manufacturer_data[0].data_len = 2;
    obs.facts.manufacturer_data[0].data[0] = 0x02;
    obs.facts.manufacturer_data[0].data[1] = 0x15;
    return obs;
}

static void test_ble_dedup(void)
{
    nearby_scan_dedup_t state;
    nearby_scan_dedup_reset(&state, 7);
    nearby_ble_normalized_t obs = sample_ble();
    assert(nearby_ble_dedup_should_emit(&state, &obs));
    obs.meta.rssi = -80; /* RSSI is not part of the duplicate identity. */
    assert(!nearby_ble_dedup_should_emit(&state, &obs));
    assert(state.ble.duplicates == 1);
    obs.facts.manufacturer_data[0].data[1] = 0x16;
    assert(nearby_ble_dedup_should_emit(&state, &obs));
    nearby_scan_dedup_reset(&state, 8);
    assert(state.generation == 8);
    assert(nearby_ble_dedup_should_emit(&state, &obs));
}

static void test_lan_dedup(void)
{
    nearby_scan_dedup_t state;
    nearby_scan_dedup_reset(&state, 1);

    nearby_mdns_service_t svc;
    memset(&svc, 0, sizeof(svc));
    strcpy(svc.service_type, "_hap._tcp.local.");
    strcpy(svc.instance, "Lamp._hap._tcp.local.");
    strcpy(svc.host, "lamp.local.");
    svc.has_port = true;
    svc.port = 12345;
    svc.txt_count = 1;
    strcpy(svc.txt[0].key, "md");
    strcpy(svc.txt[0].value, "Lamp1");
    svc.txt[0].has_value = true;
    assert(nearby_mdns_dedup_should_emit(&state, &svc));
    assert(!nearby_mdns_dedup_should_emit(&state, &svc));
    strcpy(svc.txt[0].value, "Lamp2");
    assert(nearby_mdns_dedup_should_emit(&state, &svc));

    nearby_ssdp_facts_t ssdp;
    memset(&ssdp, 0, sizeof(ssdp));
    strcpy(ssdp.usn, "uuid:abc::upnp:rootdevice");
    strcpy(ssdp.st, "upnp:rootdevice");
    strcpy(ssdp.location, "http://192.0.2.1/device.xml");
    ssdp.header_count = 5;
    assert(nearby_ssdp_dedup_should_emit(&state, &ssdp));
    ssdp.header_count = 9; /* bookkeeping is intentionally ignored. */
    assert(!nearby_ssdp_dedup_should_emit(&state, &ssdp));

    nearby_dhcp_facts_t dhcp;
    memset(&dhcp, 0, sizeof(dhcp));
    dhcp.chaddr_len = 6;
    memcpy(dhcp.chaddr, "\x01\x02\x03\x04\x05\x06", 6);
    strcpy(dhcp.hostname, "device");
    dhcp.xid = 1;
    assert(nearby_dhcp_dedup_should_emit(&state, &dhcp));
    dhcp.xid = 999; /* transaction ID does not change host identity facts. */
    assert(!nearby_dhcp_dedup_should_emit(&state, &dhcp));
}

static void test_deterministic_full_table(void)
{
    nearby_scan_dedup_t state;
    nearby_scan_dedup_reset(&state, 3);
    for (unsigned i = 0; i < NEARBY_DEDUP_SLOT_COUNT + 3u; ++i) {
        nearby_dhcp_facts_t dhcp;
        memset(&dhcp, 0, sizeof(dhcp));
        dhcp.chaddr_len = 6;
        dhcp.chaddr[0] = (uint8_t)i;
        dhcp.chaddr[5] = 1;
        assert(nearby_dhcp_dedup_should_emit(&state, &dhcp));
    }
    assert(state.dhcp.replacements == 3u);
    assert(state.dhcp.replace_cursor == 3u);
}

int main(void)
{
    test_exact_c_keys();
    test_ambiguity_fixture();
    test_ble_dedup();
    test_lan_dedup();
    test_deterministic_full_table();
    puts("dedup/matcher key host tests: OK");
    return 0;
}
