#include "nearby_lan_discovery.h"

#include <ctype.h>
#include <string.h>

#define DNS_HEADER_LEN 12
#define DNS_TYPE_A 1u
#define DNS_TYPE_PTR 12u
#define DNS_TYPE_TXT 16u
#define DNS_TYPE_AAAA 28u
#define DNS_TYPE_SRV 33u
#define MDNS_MAX_RR_WALK 32u
#define MDNS_MAX_HOST_ADDRS 6u
#define MDNS_MAX_NAME_JUMPS 16u

typedef struct {
    char host[NEARBY_MDNS_NAME_MAX + 1];
    bool has_ipv4;
    uint8_t ipv4[4];
    bool has_ipv6;
    uint8_t ipv6[16];
} host_addr_t;

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8) | p[1];
}

static int decode_name(const uint8_t *msg, size_t len, size_t start,
                       char out[NEARBY_MDNS_NAME_MAX + 1],
                       size_t *next, bool *too_long)
{
    size_t pos = start;
    size_t write = 0;
    size_t jumps = 0;
    bool jumped = false;
    bool first = true;
    *too_long = false;

    while (true) {
        if (pos >= len) {
            return -1;
        }
        const uint8_t c = msg[pos];
        if (c == 0) {
            if (!jumped) {
                *next = pos + 1;
            }
            /* HA/zeroconf service types use absolute DNS names with a trailing dot. */
            if (!first) {
                if (write < NEARBY_MDNS_NAME_MAX) {
                    out[write] = '.';
                } else {
                    *too_long = true;
                }
                ++write;
            }
            if (write <= NEARBY_MDNS_NAME_MAX) {
                out[write] = '\0';
            } else {
                out[NEARBY_MDNS_NAME_MAX] = '\0';
            }
            return 0;
        }
        if ((c & 0xc0u) == 0xc0u) {
            if (pos + 1 >= len) {
                return -1;
            }
            const size_t ptr = ((size_t)(c & 0x3fu) << 8) | msg[pos + 1];
            if (ptr >= len || ++jumps > MDNS_MAX_NAME_JUMPS) {
                return -1;
            }
            if (!jumped) {
                *next = pos + 2;
                jumped = true;
            }
            pos = ptr;
            continue;
        }
        if ((c & 0xc0u) != 0 || c > 63u || pos + 1u + c > len) {
            return -1;
        }

        if (!first) {
            if (write < NEARBY_MDNS_NAME_MAX) {
                out[write] = '.';
            } else {
                *too_long = true;
            }
            ++write;
        }
        first = false;
        for (uint8_t i = 0; i < c; ++i) {
            if (write < NEARBY_MDNS_NAME_MAX) {
                out[write] = (char)msg[pos + 1u + i];
            } else {
                *too_long = true;
            }
            ++write;
        }
        pos += 1u + c;
    }
}

static void lowercase(char *s)
{
    for (; *s != '\0'; ++s) {
        *s = (char)tolower((unsigned char)*s);
    }
}

static nearby_mdns_service_t *find_or_add_service(nearby_mdns_facts_t *out,
                                                  const char *instance)
{
    for (uint8_t i = 0; i < out->service_count; ++i) {
        if (strcmp(out->services[i].instance, instance) == 0) {
            return &out->services[i];
        }
    }
    if (out->service_count >= NEARBY_MDNS_MAX_SERVICES) {
        out->service_overflow = true;
        return NULL;
    }
    nearby_mdns_service_t *svc = &out->services[out->service_count++];
    memset(svc, 0, sizeof(*svc));
    strncpy(svc->instance, instance, NEARBY_MDNS_NAME_MAX);
    svc->instance[NEARBY_MDNS_NAME_MAX] = '\0';
    return svc;
}

static void infer_service_type(nearby_mdns_service_t *svc)
{
    if (svc->service_type[0] != '\0') {
        return;
    }
    const char *dot = strchr(svc->instance, '.');
    if (dot != NULL && dot[1] == '_') {
        strncpy(svc->service_type, dot + 1, NEARBY_MDNS_NAME_MAX);
        svc->service_type[NEARBY_MDNS_NAME_MAX] = '\0';
        lowercase(svc->service_type);
    }
}

static host_addr_t *find_or_add_host(host_addr_t *hosts, uint8_t *count,
                                     const char *name)
{
    for (uint8_t i = 0; i < *count; ++i) {
        if (strcmp(hosts[i].host, name) == 0) {
            return &hosts[i];
        }
    }
    if (*count >= MDNS_MAX_HOST_ADDRS) {
        return NULL;
    }
    host_addr_t *slot = &hosts[(*count)++];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->host, name, NEARBY_MDNS_NAME_MAX);
    slot->host[NEARBY_MDNS_NAME_MAX] = '\0';
    return slot;
}

static bool add_txt(nearby_mdns_service_t *svc,
                    const uint8_t *data, size_t len)
{
    size_t pos = 0;
    while (pos < len) {
        const uint8_t slen = data[pos++];
        if ((size_t)slen > len - pos) {
            return false;
        }
        const uint8_t *s = data + pos;
        size_t eq = 0;
        while (eq < slen && s[eq] != '=') {
            ++eq;
        }

        if (svc->txt_count >= NEARBY_MDNS_MAX_TXT) {
            svc->txt_overflow = true;
            return true;
        }
        nearby_mdns_txt_t *txt = &svc->txt[svc->txt_count++];
        memset(txt, 0, sizeof(*txt));

        const size_t key_len = eq;
        const size_t key_copy = key_len > NEARBY_MDNS_TXT_KEY_MAX
                                    ? NEARBY_MDNS_TXT_KEY_MAX : key_len;
        if (key_copy > 0) {
            memcpy(txt->key, s, key_copy);
        }
        txt->key[key_copy] = '\0';
        lowercase(txt->key);
        txt->truncated = key_copy != key_len;

        if (eq < slen) {
            txt->has_value = true;
            const size_t value_len = slen - eq - 1u;
            const size_t value_copy = value_len > NEARBY_MDNS_TXT_VALUE_MAX
                                          ? NEARBY_MDNS_TXT_VALUE_MAX
                                          : value_len;
            if (value_copy > 0) {
                memcpy(txt->value, s + eq + 1u, value_copy);
            }
            txt->value[value_copy] = '\0';
            txt->truncated = txt->truncated || value_copy != value_len;
        }
        pos += slen;
    }
    return true;
}

nearby_lan_parse_result_t nearby_mdns_parse(const uint8_t *message,
                                            size_t message_len,
                                            nearby_mdns_facts_t *out)
{
    if (out == NULL || (message == NULL && message_len != 0)) {
        return NEARBY_LAN_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (message_len < DNS_HEADER_LEN) {
        return NEARBY_LAN_PARSE_ERR_MALFORMED;
    }

    const uint16_t qd = be16(message + 4);
    const uint16_t an = be16(message + 6);
    const uint16_t ns = be16(message + 8);
    const uint16_t ar = be16(message + 10);
    const uint32_t total_rr = (uint32_t)an + ns + ar;

    bool partial = false;
    size_t pos = DNS_HEADER_LEN;

    for (uint16_t i = 0; i < qd; ++i) {
        char name[NEARBY_MDNS_NAME_MAX + 1];
        size_t next = 0;
        bool too_long = false;
        if (decode_name(message, message_len, pos, name, &next, &too_long) != 0 ||
            next + 4u > message_len) {
            return NEARBY_LAN_PARSE_ERR_MALFORMED;
        }
        if (too_long) {
            out->name_overflow = true;
            partial = true;
        }
        pos = next + 4u;
    }

    host_addr_t hosts[MDNS_MAX_HOST_ADDRS];
    uint8_t host_count = 0;
    memset(hosts, 0, sizeof(hosts));

    const uint32_t walk = total_rr > MDNS_MAX_RR_WALK
                              ? MDNS_MAX_RR_WALK : total_rr;
    if (walk != total_rr) {
        out->rr_walk_limited = true;
        partial = true;
    }

    for (uint32_t i = 0; i < walk; ++i) {
        char owner[NEARBY_MDNS_NAME_MAX + 1];
        size_t next = 0;
        bool owner_long = false;
        if (decode_name(message, message_len, pos, owner, &next, &owner_long) != 0 ||
            next + 10u > message_len) {
            return NEARBY_LAN_PARSE_ERR_MALFORMED;
        }
        const uint16_t type = be16(message + next);
        const uint16_t rdlen = be16(message + next + 8);
        const size_t rdata = next + 10u;
        if ((size_t)rdlen > message_len - rdata) {
            return NEARBY_LAN_PARSE_ERR_MALFORMED;
        }
        const size_t rr_end = rdata + rdlen;
        pos = rr_end;

        if (owner_long) {
            out->name_overflow = true;
            partial = true;
            continue;
        }

        if (type == DNS_TYPE_PTR) {
            char target[NEARBY_MDNS_NAME_MAX + 1];
            size_t target_next = 0;
            bool target_long = false;
            if (decode_name(message, message_len, rdata, target,
                            &target_next, &target_long) != 0 ||
                target_next > rr_end) {
                return NEARBY_LAN_PARSE_ERR_MALFORMED;
            }
            if (target_long) {
                out->name_overflow = true;
                partial = true;
                continue;
            }
            nearby_mdns_service_t *svc = find_or_add_service(out, target);
            if (svc != NULL) {
                strncpy(svc->service_type, owner, NEARBY_MDNS_NAME_MAX);
                svc->service_type[NEARBY_MDNS_NAME_MAX] = '\0';
                lowercase(svc->service_type);
            }
        } else if (type == DNS_TYPE_SRV) {
            if (rdlen < 6u) {
                return NEARBY_LAN_PARSE_ERR_MALFORMED;
            }
            char host[NEARBY_MDNS_NAME_MAX + 1];
            size_t host_next = 0;
            bool host_long = false;
            if (decode_name(message, message_len, rdata + 6u, host,
                            &host_next, &host_long) != 0 ||
                host_next > rr_end) {
                return NEARBY_LAN_PARSE_ERR_MALFORMED;
            }
            if (host_long) {
                out->name_overflow = true;
                partial = true;
                continue;
            }
            lowercase(host);
            nearby_mdns_service_t *svc = find_or_add_service(out, owner);
            if (svc != NULL) {
                svc->port = be16(message + rdata + 4u);
                svc->has_port = true;
                strncpy(svc->host, host, NEARBY_MDNS_NAME_MAX);
                svc->host[NEARBY_MDNS_NAME_MAX] = '\0';
                infer_service_type(svc);
            }
        } else if (type == DNS_TYPE_TXT) {
            nearby_mdns_service_t *svc = find_or_add_service(out, owner);
            if (svc != NULL) {
                if (!add_txt(svc, message + rdata, rdlen)) {
                    return NEARBY_LAN_PARSE_ERR_MALFORMED;
                }
                infer_service_type(svc);
                if (svc->txt_overflow) {
                    partial = true;
                }
            }
        } else if (type == DNS_TYPE_A && rdlen == 4u) {
            lowercase(owner);
            host_addr_t *host = find_or_add_host(hosts, &host_count, owner);
            if (host != NULL) {
                host->has_ipv4 = true;
                memcpy(host->ipv4, message + rdata, 4);
            } else {
                partial = true;
            }
        } else if (type == DNS_TYPE_AAAA && rdlen == 16u) {
            lowercase(owner);
            host_addr_t *host = find_or_add_host(hosts, &host_count, owner);
            if (host != NULL) {
                host->has_ipv6 = true;
                memcpy(host->ipv6, message + rdata, 16);
            } else {
                partial = true;
            }
        }
    }

    for (uint8_t i = 0; i < out->service_count; ++i) {
        nearby_mdns_service_t *svc = &out->services[i];
        infer_service_type(svc);
        for (uint8_t j = 0; j < host_count; ++j) {
            if (svc->host[0] != '\0' &&
                strcmp(svc->host, hosts[j].host) == 0) {
                svc->has_ipv4 = hosts[j].has_ipv4;
                memcpy(svc->ipv4, hosts[j].ipv4, sizeof(svc->ipv4));
                svc->has_ipv6 = hosts[j].has_ipv6;
                memcpy(svc->ipv6, hosts[j].ipv6, sizeof(svc->ipv6));
                break;
            }
        }
    }

    partial = partial || out->service_overflow || out->name_overflow;
    return partial ? NEARBY_LAN_PARSE_PARTIAL : NEARBY_LAN_PARSE_OK;
}
