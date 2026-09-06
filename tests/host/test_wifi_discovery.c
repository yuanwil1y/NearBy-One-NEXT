#include "nearby_wifi_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t make_header(uint8_t *buf, size_t cap, uint16_t frame_control)
{
    assert(cap >= 24);
    memset(buf, 0, cap);
    buf[0] = (uint8_t)frame_control;
    buf[1] = (uint8_t)(frame_control >> 8);
    for (unsigned i = 0; i < 6; ++i) {
        buf[4 + i] = 0xff;
        buf[10 + i] = (uint8_t)(0x10 + i);
        buf[16 + i] = (uint8_t)(0x20 + i);
    }
    return 24;
}

static void test_beacon(void)
{
    uint8_t frame[256];
    size_t p = make_header(frame, sizeof(frame), 0x0080u);
    memset(frame + p, 0, 8);
    p += 8;
    frame[p++] = 0x64;
    frame[p++] = 0x00;
    frame[p++] = 0x31;
    frame[p++] = 0x04;

    const uint8_t ies[] = {
        0, 4, 'T', 'e', 's', 't',
        1, 2, 0x82, 0x84,
        3, 1, 6,
        48, 2, 1, 0,
        221, 6, 0x00, 0x50, 0xf2, 0x04, 0x10, 0x4a,
        255, 1, 35,
    };
    memcpy(frame + p, ies, sizeof(ies));
    p += sizeof(ies);

    nearby_wifi_mgmt_facts_t facts;
    assert(nearby_wifi_parse_management_frame(frame, p, false, &facts) == NEARBY_WIFI_PARSE_OK);
    assert(facts.subtype == NEARBY_WIFI_MGMT_BEACON);
    assert(facts.has_ssid && facts.ssid_len == 4);
    assert(memcmp(facts.ssid, "Test", 4) == 0);
    assert(facts.has_channel && facts.channel == 6);
    assert(facts.has_beacon_interval && facts.beacon_interval_tu == 100);
    assert(facts.has_capability && facts.capability == 0x0431);
    assert(facts.supported_rate_count == 2);
    assert(facts.has_rsn && facts.has_wps && facts.has_he);
    assert(facts.vendor_ie_count == 1);
}

static void test_probe_request_hidden_ssid(void)
{
    uint8_t frame[64];
    size_t p = make_header(frame, sizeof(frame), 0x0040u);
    frame[p++] = 0;
    frame[p++] = 0;
    nearby_wifi_mgmt_facts_t facts;
    assert(nearby_wifi_parse_management_frame(frame, p, false, &facts) == NEARBY_WIFI_PARSE_OK);
    assert(facts.subtype == NEARBY_WIFI_MGMT_PROBE_REQ);
    assert(facts.has_ssid && facts.hidden_ssid && facts.ssid_len == 0);
}

static void test_unsupported_data_frame(void)
{
    uint8_t frame[32];
    const size_t p = make_header(frame, sizeof(frame), 0x0008u);
    nearby_wifi_mgmt_facts_t facts;
    assert(nearby_wifi_parse_management_frame(frame, p, false, &facts) == NEARBY_WIFI_PARSE_UNSUPPORTED);
}

static void test_truncated_ie_policy(void)
{
    uint8_t frame[64];
    size_t p = make_header(frame, sizeof(frame), 0x0040u);
    frame[p++] = 0;
    frame[p++] = 5;
    frame[p++] = 'A';
    nearby_wifi_mgmt_facts_t facts;
    assert(nearby_wifi_parse_management_frame(frame, p, true, &facts) == NEARBY_WIFI_PARSE_PARTIAL);
    assert(facts.partial_tail);
    assert(nearby_wifi_parse_management_frame(frame, p, false, &facts) == NEARBY_WIFI_PARSE_ERR_MALFORMED);
}

static void test_invalid_ssid_length(void)
{
    uint8_t frame[96];
    size_t p = make_header(frame, sizeof(frame), 0x0040u);
    frame[p++] = 0;
    frame[p++] = 33;
    memset(frame + p, 'x', 33);
    p += 33;
    nearby_wifi_mgmt_facts_t facts;
    assert(nearby_wifi_parse_management_frame(frame, p, false, &facts) == NEARBY_WIFI_PARSE_ERR_MALFORMED);
}

static void test_vendor_overflow_is_partial(void)
{
    uint8_t frame[128];
    size_t p = make_header(frame, sizeof(frame), 0x0040u);
    for (unsigned i = 0; i < NEARBY_WIFI_MAX_VENDOR_IES + 1u; ++i) {
        frame[p++] = 221;
        frame[p++] = 3;
        frame[p++] = 0x01;
        frame[p++] = 0x02;
        frame[p++] = (uint8_t)i;
    }
    nearby_wifi_mgmt_facts_t facts;
    assert(nearby_wifi_parse_management_frame(frame, p, false, &facts) == NEARBY_WIFI_PARSE_PARTIAL);
    assert(facts.vendor_ie_overflow);
    assert(facts.vendor_ie_count == NEARBY_WIFI_MAX_VENDOR_IES);
}

int main(void)
{
    test_beacon();
    test_probe_request_hidden_ssid();
    test_unsupported_data_frame();
    test_truncated_ie_policy();
    test_invalid_ssid_length();
    test_vendor_overflow_is_partial();
    puts("wifi discovery host tests: OK");
    return 0;
}
