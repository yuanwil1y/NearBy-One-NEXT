#include "nearby_lan_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t put_name(uint8_t *buf, size_t off, const char *name)
{
    const char *p = name;
    while (*p != '\0') {
        const char *dot = strchr(p, '.');
        size_t n = dot ? (size_t)(dot - p) : strlen(p);
        if (n == 0) {
            break;
        }
        assert(n <= 63);
        buf[off++] = (uint8_t)n;
        memcpy(buf + off, p, n);
        off += n;
        if (!dot) {
            break;
        }
        p = dot + 1;
    }
    buf[off++] = 0;
    return off;
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static size_t rr_header(uint8_t *buf, size_t off, const char *owner,
                        uint16_t type, uint16_t rdlen)
{
    off = put_name(buf, off, owner);
    put_be16(buf + off, type); off += 2;
    put_be16(buf + off, 1); off += 2;
    put_be32(buf + off, 120); off += 4;
    put_be16(buf + off, rdlen); off += 2;
    return off;
}

static void test_mdns_hap_service(void)
{
    uint8_t msg[512] = {0};
    put_be16(msg + 2, 0x8400);
    put_be16(msg + 6, 4);
    size_t off = 12;

    uint8_t rdata[160];
    size_t rlen = put_name(rdata, 0, "Lamp._hap._tcp.local.");
    off = rr_header(msg, off, "_hap._tcp.local.", 12, (uint16_t)rlen);
    memcpy(msg + off, rdata, rlen); off += rlen;

    size_t srv_len = 6;
    memset(rdata, 0, 4);
    put_be16(rdata + 4, 12345);
    srv_len = put_name(rdata, srv_len, "lamp.local.");
    off = rr_header(msg, off, "Lamp._hap._tcp.local.", 33, (uint16_t)srv_len);
    memcpy(msg + off, rdata, srv_len); off += srv_len;

    const char *a = "md=Model1";
    const char *b = "id=abc";
    rlen = 0;
    rdata[rlen++] = (uint8_t)strlen(a);
    memcpy(rdata + rlen, a, strlen(a)); rlen += strlen(a);
    rdata[rlen++] = (uint8_t)strlen(b);
    memcpy(rdata + rlen, b, strlen(b)); rlen += strlen(b);
    off = rr_header(msg, off, "Lamp._hap._tcp.local.", 16, (uint16_t)rlen);
    memcpy(msg + off, rdata, rlen); off += rlen;

    off = rr_header(msg, off, "lamp.local.", 1, 4);
    msg[off++] = 192; msg[off++] = 168; msg[off++] = 1; msg[off++] = 2;

    nearby_mdns_facts_t facts;
    assert(nearby_mdns_parse(msg, off, &facts) == NEARBY_LAN_PARSE_OK);
    assert(facts.service_count == 1);
    const nearby_mdns_service_t *svc = &facts.services[0];
    assert(strcmp(svc->service_type, "_hap._tcp.local.") == 0);
    assert(strcmp(svc->instance, "Lamp._hap._tcp.local.") == 0);
    assert(strcmp(svc->host, "lamp.local.") == 0);
    assert(svc->has_port && svc->port == 12345);
    assert(svc->txt_count == 2);
    assert(strcmp(svc->txt[0].key, "md") == 0);
    assert(strcmp(svc->txt[0].value, "Model1") == 0);
    assert(strcmp(svc->txt[1].key, "id") == 0);
    assert(strcmp(svc->txt[1].value, "abc") == 0);
    assert(svc->has_ipv4);
    assert(memcmp(svc->ipv4, (uint8_t[]){192,168,1,2}, 4) == 0);
}

static void test_mdns_compression_loop_rejected(void)
{
    uint8_t msg[24] = {0};
    put_be16(msg + 6, 1);
    msg[12] = 0xc0;
    msg[13] = 12;
    nearby_mdns_facts_t facts;
    assert(nearby_mdns_parse(msg, sizeof(msg), &facts) == NEARBY_LAN_PARSE_ERR_MALFORMED);
}

static void test_mdns_bad_txt_rejected(void)
{
    uint8_t msg[128] = {0};
    put_be16(msg + 6, 1);
    size_t off = rr_header(msg, 12, "X._http._tcp.local.", 16, 3);
    msg[off++] = 5; msg[off++] = 'a'; msg[off++] = 'b';
    nearby_mdns_facts_t facts;
    assert(nearby_mdns_parse(msg, off, &facts) == NEARBY_LAN_PARSE_ERR_MALFORMED);
}

static void test_ssdp_headers(void)
{
    const char packet[] =
        "HTTP/1.1 200 OK\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "uSn: uuid:abc::urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "SERVER: TestOS/1 UPnP/1.1 Device/2\r\n"
        "LOCATION: http://192.168.1.10/device.xml\r\n"
        "Manufacturer: Example Co\r\n"
        "deviceType: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
        "\r\n";
    nearby_ssdp_facts_t facts;
    assert(nearby_ssdp_parse((const uint8_t *)packet, strlen(packet), &facts) == NEARBY_LAN_PARSE_OK);
    assert(strcmp(facts.start_line, "HTTP/1.1 200 OK") == 0);
    assert(strcmp(facts.st, "urn:schemas-upnp-org:device:MediaRenderer:1") == 0);
    assert(strncmp(facts.usn, "uuid:abc", 8) == 0);
    assert(strcmp(facts.manufacturer, "Example Co") == 0);
    assert(strcmp(facts.device_type, "urn:schemas-upnp-org:device:MediaRenderer:1") == 0);
}

static void test_ssdp_malformed_header(void)
{
    const char packet[] = "NOTIFY * HTTP/1.1\r\nBadHeader\r\n\r\n";
    nearby_ssdp_facts_t facts;
    assert(nearby_ssdp_parse((const uint8_t *)packet, strlen(packet), &facts) == NEARBY_LAN_PARSE_ERR_MALFORMED);
}

static void test_dhcp_facts(void)
{
    uint8_t msg[320] = {0};
    msg[0] = 1; msg[1] = 1; msg[2] = 6;
    put_be32(msg + 4, 0x12345678);
    const uint8_t mac[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    memcpy(msg + 28, mac, sizeof(mac));
    put_be32(msg + 236, 0x63825363);
    size_t off = 240;
    msg[off++] = 53; msg[off++] = 1; msg[off++] = 1;
    const char *host = "nearby-device";
    msg[off++] = 12; msg[off++] = (uint8_t)strlen(host);
    memcpy(msg + off, host, strlen(host)); off += strlen(host);
    const char *vendor = "vendor-class";
    msg[off++] = 60; msg[off++] = (uint8_t)strlen(vendor);
    memcpy(msg + off, vendor, strlen(vendor)); off += strlen(vendor);
    const uint8_t cid[] = {1,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    msg[off++] = 61; msg[off++] = sizeof(cid);
    memcpy(msg + off, cid, sizeof(cid)); off += sizeof(cid);
    msg[off++] = 255;

    nearby_dhcp_facts_t facts;
    assert(nearby_dhcp_parse(msg, off, &facts) == NEARBY_LAN_PARSE_OK);
    assert(facts.xid == 0x12345678);
    assert(facts.chaddr_len == 6 && memcmp(facts.chaddr, mac, 6) == 0);
    assert(facts.has_message_type && facts.message_type == 1);
    assert(strcmp(facts.hostname, host) == 0);
    assert(strcmp(facts.vendor_class, vendor) == 0);
    assert(facts.client_identifier_len == sizeof(cid));
    assert(facts.options_ended);
}

static void test_dhcp_option_overrun(void)
{
    uint8_t msg[244] = {0};
    msg[2] = 6;
    put_be32(msg + 236, 0x63825363);
    msg[240] = 12; msg[241] = 10; msg[242] = 'x'; msg[243] = 'y';
    nearby_dhcp_facts_t facts;
    assert(nearby_dhcp_parse(msg, sizeof(msg), &facts) == NEARBY_LAN_PARSE_ERR_MALFORMED);
}

int main(void)
{
    test_mdns_hap_service();
    test_mdns_compression_loop_rejected();
    test_mdns_bad_txt_rejected();
    test_ssdp_headers();
    test_ssdp_malformed_header();
    test_dhcp_facts();
    test_dhcp_option_overrun();
    puts("lan discovery host tests: OK");
    return 0;
}
