#include "nearby_lan_discovery.h"

#include <ctype.h>
#include <string.h>

#define DHCP_FIXED_LEN 240
#define DHCP_MAGIC_COOKIE 0x63825363u

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void lowercase_ascii(char *s)
{
    for (; *s != '\0'; ++s) {
        *s = (char)tolower((unsigned char)*s);
    }
}

static void copy_string(char *dst, size_t cap, bool *truncated,
                        const uint8_t *src, size_t len)
{
    const size_t n = len >= cap ? cap - 1 : len;
    if (n > 0) {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
    *truncated = n != len;
}

nearby_lan_parse_result_t nearby_dhcp_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_dhcp_facts_t *out)
{
    if (out == NULL || (message == NULL && message_len != 0)) {
        return NEARBY_LAN_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (message_len < DHCP_FIXED_LEN) {
        return NEARBY_LAN_PARSE_ERR_MALFORMED;
    }

    out->op = message[0];
    out->htype = message[1];
    out->hlen = message[2];
    out->xid = be32(message + 4);
    if (out->hlen > 16) {
        return NEARBY_LAN_PARSE_ERR_MALFORMED;
    }
    out->chaddr_len = out->hlen;
    if (out->chaddr_len > 0) {
        memcpy(out->chaddr, message + 28, out->chaddr_len);
    }

    if (be32(message + 236) != DHCP_MAGIC_COOKIE) {
        return NEARBY_LAN_PARSE_ERR_MALFORMED;
    }

    bool partial = false;
    size_t pos = DHCP_FIXED_LEN;
    while (pos < message_len) {
        const uint8_t code = message[pos++];
        if (code == 0) {
            continue;
        }
        if (code == 255) {
            out->options_ended = true;
            break;
        }
        if (pos >= message_len) {
            return NEARBY_LAN_PARSE_ERR_MALFORMED;
        }
        const uint8_t len = message[pos++];
        if ((size_t)len > message_len - pos) {
            return NEARBY_LAN_PARSE_ERR_MALFORMED;
        }
        const uint8_t *value = message + pos;

        switch (code) {
        case 12:
            copy_string(out->hostname, sizeof(out->hostname),
                        &out->hostname_truncated, value, len);
            lowercase_ascii(out->hostname);
            partial = partial || out->hostname_truncated;
            break;
        case 53:
            if (len != 1) {
                return NEARBY_LAN_PARSE_ERR_MALFORMED;
            }
            out->has_message_type = true;
            out->message_type = value[0];
            break;
        case 60:
            copy_string(out->vendor_class, sizeof(out->vendor_class),
                        &out->vendor_class_truncated, value, len);
            partial = partial || out->vendor_class_truncated;
            break;
        case 61: {
            const size_t n = len > NEARBY_DHCP_CLIENT_ID_MAX
                                 ? NEARBY_DHCP_CLIENT_ID_MAX
                                 : len;
            if (n > 0) {
                memcpy(out->client_identifier, value, n);
            }
            out->client_identifier_len = (uint8_t)n;
            out->client_identifier_truncated = n != len;
            partial = partial || out->client_identifier_truncated;
            break;
        }
        default:
            break;
        }
        pos += len;
    }

    return partial ? NEARBY_LAN_PARSE_PARTIAL : NEARBY_LAN_PARSE_OK;
}
