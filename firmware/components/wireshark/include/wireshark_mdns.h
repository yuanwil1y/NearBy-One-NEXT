#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wireshark_lan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIRESHARK_MDNS_NAME_MAX 80
#define WIRESHARK_MDNS_MAX_SERVICES 6
#define WIRESHARK_MDNS_MAX_TXT 8
#define WIRESHARK_MDNS_TXT_KEY_MAX 24
#define WIRESHARK_MDNS_TXT_VALUE_MAX 48

typedef struct {
    char key[WIRESHARK_MDNS_TXT_KEY_MAX + 1];
    char value[WIRESHARK_MDNS_TXT_VALUE_MAX + 1];
    bool has_value;
    bool truncated;
} wireshark_mdns_txt_t;

typedef struct {
    char service_type[WIRESHARK_MDNS_NAME_MAX + 1];
    char instance[WIRESHARK_MDNS_NAME_MAX + 1];
    char host[WIRESHARK_MDNS_NAME_MAX + 1];
    uint16_t port;
    bool has_port;
    wireshark_mdns_txt_t txt[WIRESHARK_MDNS_MAX_TXT];
    uint8_t txt_count;
    bool txt_overflow;
    bool has_ipv4;
    uint8_t ipv4[4];
    bool has_ipv6;
    uint8_t ipv6[16];
} wireshark_mdns_service_t;

typedef struct {
    wireshark_mdns_service_t services[WIRESHARK_MDNS_MAX_SERVICES];
    uint8_t service_count;
    bool service_overflow;
    bool name_overflow;
    bool rr_walk_limited;
} wireshark_mdns_facts_t;

/* Phase 1 contract; implementation remains under nearby_mdns_parse(). */
wireshark_lan_parse_result_t wireshark_mdns_parse(
    const uint8_t *message,
    size_t message_len,
    wireshark_mdns_facts_t *out);

#ifdef __cplusplus
}
#endif
