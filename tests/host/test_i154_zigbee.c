#include "nearby_i154_discovery.h"
#include "nearby_zigbee_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put_le16(uint8_t *buf, size_t *p, uint16_t v)
{
    buf[(*p)++] = (uint8_t)v;
    buf[(*p)++] = (uint8_t)(v >> 8);
}

static size_t start_mac(uint8_t *buf, uint16_t fcf)
{
    size_t p = 0;
    put_le16(buf, &p, fcf);
    buf[p++] = 0x5a;
    put_le16(buf, &p, 0x1234);
    put_le16(buf, &p, 0xffff);
    put_le16(buf, &p, 0x5678);
    return p;
}

static size_t append_zigbee_aps(uint8_t *buf, size_t p,
                                uint16_t profile, uint16_t cluster,
                                const uint8_t *app, size_t app_len)
{
    put_le16(buf, &p, 0x0008); /* NWK data, protocol version 2. */
    put_le16(buf, &p, 0xffff);
    put_le16(buf, &p, 0x1234);
    buf[p++] = 1;
    buf[p++] = 2;

    buf[p++] = 0x00; /* APS data, unicast, no security. */
    buf[p++] = profile == 0 ? 0 : 1;
    put_le16(buf, &p, cluster);
    put_le16(buf, &p, profile);
    buf[p++] = profile == 0 ? 0 : 1;
    buf[p++] = 3;
    memcpy(buf + p, app, app_len);
    return p + app_len;
}

static void test_device_announce(void)
{
    uint8_t psdu[128];
    size_t p = start_mac(psdu, 0x8841);
    const uint8_t announce[] = {
        4, 0x34, 0x12,
        1, 2, 3, 4, 5, 6, 7, 8,
        0x8e,
    };
    p = append_zigbee_aps(psdu, p, 0x0000, 0x0013,
                          announce, sizeof(announce));

    nearby_i154_mac_facts_t mac;
    assert(nearby_i154_parse_mac(psdu, p, &mac) == NEARBY_I154_PARSE_OK);
    assert(mac.frame_type == 1 && mac.destination_addr_len == 2 && mac.source_addr_len == 2);
    assert(mac.has_destination_pan && mac.has_source_pan);
    assert(mac.destination_pan == 0x1234 && mac.source_pan == 0x1234);

    nearby_zigbee_discovery_facts_t z;
    assert(nearby_zigbee_parse_from_mac(psdu, p, &mac, &z) == NEARBY_ZIGBEE_PARSE_OK);
    assert(z.has_device_announce);
    assert(z.announced_nwk == 0x1234);
    assert(memcmp(z.announced_ieee, "\x01\x02\x03\x04\x05\x06\x07\x08", 8) == 0);
    assert(z.announced_capability == 0x8e);
}

static void test_simple_descriptor(void)
{
    uint8_t psdu[128];
    size_t p = start_mac(psdu, 0x8841);
    const uint8_t simple[] = {
        1, 0, 0x34, 0x12, 12,
        1, 0x04, 0x01, 0x00, 0x01, 1,
        1, 0x00, 0x00,
        1, 0x06, 0x00,
    };
    p = append_zigbee_aps(psdu, p, 0x0000, 0x8004,
                          simple, sizeof(simple));
    nearby_i154_mac_facts_t mac;
    nearby_zigbee_discovery_facts_t z;
    assert(nearby_i154_parse_mac(psdu, p, &mac) == NEARBY_I154_PARSE_OK);
    assert(nearby_zigbee_parse_from_mac(psdu, p, &mac, &z) == NEARBY_ZIGBEE_PARSE_OK);
    assert(z.has_simple_descriptor);
    assert(z.descriptor_endpoint == 1);
    assert(z.descriptor_profile_id == 0x0104);
    assert(z.descriptor_device_id == 0x0100);
    assert(z.input_cluster_count == 1 && z.input_clusters[0] == 0x0000);
    assert(z.output_cluster_count == 1 && z.output_clusters[0] == 0x0006);
}

static void test_basic_cluster_identity(void)
{
    uint8_t psdu[160];
    size_t p = start_mac(psdu, 0x8841);
    const uint8_t report[] = {
        0x18, 0x22, 0x0a,
        0x04, 0x00, 0x42, 4, 'A', 'c', 'm', 'e',
        0x05, 0x00, 0x42, 6, 'M', 'o', 'd', 'e', 'l', '1',
    };
    p = append_zigbee_aps(psdu, p, 0x0104, 0x0000,
                          report, sizeof(report));
    nearby_i154_mac_facts_t mac;
    nearby_zigbee_discovery_facts_t z;
    assert(nearby_i154_parse_mac(psdu, p, &mac) == NEARBY_I154_PARSE_OK);
    assert(nearby_zigbee_parse_from_mac(psdu, p, &mac, &z) == NEARBY_ZIGBEE_PARSE_OK);
    assert(z.has_manufacturer && strcmp(z.manufacturer, "Acme") == 0);
    assert(z.has_model && strcmp(z.model, "Model1") == 0);
}

static void test_secured_mac_stops_above_mac(void)
{
    uint8_t psdu[64];
    size_t p = start_mac(psdu, 0x8849); /* security bit set */
    psdu[p++] = 0x08; /* key id mode 1 */
    psdu[p++] = 1;
    psdu[p++] = 2;
    psdu[p++] = 3;
    psdu[p++] = 4;
    psdu[p++] = 7;
    nearby_i154_mac_facts_t mac;
    assert(nearby_i154_parse_mac(psdu, p, &mac) == NEARBY_I154_PARSE_SECURED);
    assert(mac.security_enabled);
}

static void test_unsupported_2015_header(void)
{
    uint8_t psdu[32];
    const size_t p = start_mac(psdu, 0xa841);
    nearby_i154_mac_facts_t mac;
    assert(nearby_i154_parse_mac(psdu, p, &mac) == NEARBY_I154_PARSE_UNSUPPORTED);
}

static void test_malformed_short_address(void)
{
    const uint8_t psdu[] = {0x41, 0x88, 1, 0x34};
    nearby_i154_mac_facts_t mac;
    assert(nearby_i154_parse_mac(psdu, sizeof(psdu), &mac) == NEARBY_I154_PARSE_ERR_MALFORMED);
}

int main(void)
{
    test_device_announce();
    test_simple_descriptor();
    test_basic_cluster_identity();
    test_secured_mac_stops_above_mac();
    test_unsupported_2015_header();
    test_malformed_short_address();
    puts("802.15.4/zigbee host tests: OK");
    return 0;
}
