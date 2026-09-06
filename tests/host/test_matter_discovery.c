#include "nearby_ble_discovery.h"
#include "nearby_matcher_keys.h"
#include "nearby_matter_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_ble_commissioning(void)
{
    const uint8_t adv[] = {
        11, 0x16, 0xf6, 0xff,
        0x01,       /* opcode */
        0xbc, 0x2a, /* discriminator 0xabc, adv version 2 */
        0x34, 0x12, /* VID 0x1234 */
        0xab, 0x00, /* PID 0x00ab */
        0x03,       /* additional data flags */
    };
    nearby_ble_normalized_t ble;
    memset(&ble, 0, sizeof(ble));
    assert(nearby_ble_parse_advertisement(adv, sizeof(adv), false, &ble.facts) == NEARBY_BLE_PARSE_OK);

    nearby_matter_discovery_facts_t matter;
    assert(nearby_matter_from_ble(&ble, &matter) == NEARBY_MATTER_PARSE_OK);
    assert(matter.source == NEARBY_MATTER_SOURCE_BLE);
    assert(matter.has_discriminator && matter.discriminator == 0x0abc);
    assert(matter.has_advertisement_version && matter.advertisement_version == 2);
    assert(matter.has_vendor_product && matter.vendor_id == 0x1234 && matter.product_id == 0x00ab);
    assert(matter.has_opcode && matter.opcode == 1);
    assert(matter.has_additional_data_flags && matter.additional_data_flags == 3);

    char key[NEARBY_MATCHER_KEY_MAX];
    assert(nearby_match_key_matter_vidpid(matter.vendor_id, matter.product_id,
                                          key, sizeof(key)) == NEARBY_MATCHER_KEY_OK);
    assert(strcmp(key, "matter:vidpid:1234:00ab") == 0);
}

static void add_txt(nearby_mdns_service_t *svc, const char *key, const char *value)
{
    nearby_mdns_txt_t *txt = &svc->txt[svc->txt_count++];
    memset(txt, 0, sizeof(*txt));
    strcpy(txt->key, key);
    strcpy(txt->value, value);
    txt->has_value = true;
}

static void test_mdns_commissioning(void)
{
    nearby_mdns_service_t svc;
    memset(&svc, 0, sizeof(svc));
    strcpy(svc.service_type, "_MatterC._UDP.local.");
    add_txt(&svc, "D", "2748");
    add_txt(&svc, "VP", "4660+171");
    add_txt(&svc, "CM", "1");
    add_txt(&svc, "DT", "35");

    nearby_matter_discovery_facts_t matter;
    assert(nearby_matter_from_mdns(&svc, &matter) == NEARBY_MATTER_PARSE_OK);
    assert(matter.source == NEARBY_MATTER_SOURCE_MDNS);
    assert(matter.has_discriminator && matter.discriminator == 2748);
    assert(matter.has_vendor_product && matter.vendor_id == 4660 && matter.product_id == 171);
    assert(matter.has_commissioning_mode && matter.commissioning_mode == 1);
    assert(matter.has_device_type && matter.device_type == 35);
}

static void test_malformed_mdns_values(void)
{
    nearby_mdns_service_t svc;
    memset(&svc, 0, sizeof(svc));
    strcpy(svc.service_type, "_matterc._udp.local.");
    add_txt(&svc, "D", "4096");
    nearby_matter_discovery_facts_t matter;
    assert(nearby_matter_from_mdns(&svc, &matter) == NEARBY_MATTER_PARSE_ERR_MALFORMED);

    memset(&svc, 0, sizeof(svc));
    strcpy(svc.service_type, "_matterc._udp.local.");
    add_txt(&svc, "VP", "65536+1");
    assert(nearby_matter_from_mdns(&svc, &matter) == NEARBY_MATTER_PARSE_ERR_MALFORMED);
}

static void test_not_matter(void)
{
    nearby_mdns_service_t svc;
    memset(&svc, 0, sizeof(svc));
    strcpy(svc.service_type, "_http._tcp.local.");
    nearby_matter_discovery_facts_t matter;
    assert(nearby_matter_from_mdns(&svc, &matter) == NEARBY_MATTER_PARSE_NOT_MATTER);

    nearby_ble_normalized_t ble;
    memset(&ble, 0, sizeof(ble));
    assert(nearby_matter_from_ble(&ble, &matter) == NEARBY_MATTER_PARSE_NOT_MATTER);
}

int main(void)
{
    test_ble_commissioning();
    test_mdns_commissioning();
    test_malformed_mdns_values();
    test_not_matter();
    puts("matter discovery host tests: OK");
    return 0;
}
