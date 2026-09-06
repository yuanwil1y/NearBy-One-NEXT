#include "nearby_lan_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static size_t name(uint8_t *buf, size_t pos, const char *s)
{
    const char *part = s;
    while (*part) {
        const char *dot = strchr(part, '.');
        size_t len = dot ? (size_t)(dot - part) : strlen(part);
        if (len == 0) break;
        buf[pos++] = (uint8_t)len;
        memcpy(buf + pos, part, len);
        pos += len;
        if (!dot) break;
        part = dot + 1;
    }
    buf[pos++] = 0;
    return pos;
}

static size_t rr(uint8_t *buf, size_t pos, const char *owner,
                 uint16_t type, const uint8_t *rdata, uint16_t rlen)
{
    pos = name(buf, pos, owner);
    be16(buf + pos, type); pos += 2;
    be16(buf + pos, 1); pos += 2;
    be32(buf + pos, 120); pos += 4;
    be16(buf + pos, rlen); pos += 2;
    memcpy(buf + pos, rdata, rlen);
    return pos + rlen;
}

static void test_mdns_preserves_matcher_key_case(void)
{
    uint8_t msg[256] = {0};
    be16(msg + 6, 2);
    size_t pos = 12;
    uint8_t data[128];
    size_t n = name(data, 0, "Unit._PowerView-G3._tcp.local.");
    pos = rr(msg, pos, "_PowerView-G3._tcp.local.", 12, data, (uint16_t)n);
    const char txt[] = "MT=Shade";
    data[0] = (uint8_t)strlen(txt);
    memcpy(data + 1, txt, strlen(txt));
    pos = rr(msg, pos, "Unit._PowerView-G3._tcp.local.", 16,
             data, (uint16_t)(strlen(txt) + 1));

    nearby_mdns_facts_t facts;
    assert(nearby_mdns_parse(msg, pos, &facts) == NEARBY_LAN_PARSE_OK);
    assert(facts.service_count == 1);
    assert(strcmp(facts.services[0].service_type, "_PowerView-G3._tcp.local.") == 0);
    assert(facts.services[0].txt_count == 1);
    assert(strcmp(facts.services[0].txt[0].key, "MT") == 0);
    assert(strcmp(facts.services[0].txt[0].value, "Shade") == 0);
}

static void test_dhcp_hostname_lowercase(void)
{
    uint8_t msg[300] = {0};
    msg[0] = 1; msg[1] = 1; msg[2] = 6;
    be32(msg + 236, 0x63825363u);
    size_t pos = 240;
    const char host[] = "NearBy-Device";
    msg[pos++] = 12;
    msg[pos++] = (uint8_t)strlen(host);
    memcpy(msg + pos, host, strlen(host));
    pos += strlen(host);
    msg[pos++] = 255;

    nearby_dhcp_facts_t facts;
    assert(nearby_dhcp_parse(msg, pos, &facts) == NEARBY_LAN_PARSE_OK);
    assert(strcmp(facts.hostname, "nearby-device") == 0);
}

int main(void)
{
    test_mdns_preserves_matcher_key_case();
    test_dhcp_hostname_lowercase();
    puts("HA normalization host tests: OK");
    return 0;
}
