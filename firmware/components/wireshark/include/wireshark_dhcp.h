#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wireshark_lan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIRESHARK_DHCP_HOSTNAME_MAX 64
#define WIRESHARK_DHCP_VENDOR_MAX 64
#define WIRESHARK_DHCP_CLIENT_ID_MAX 32

typedef struct {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint32_t xid;
    uint8_t chaddr[16];
    uint8_t chaddr_len;
    bool has_message_type;
    uint8_t message_type;
    char hostname[WIRESHARK_DHCP_HOSTNAME_MAX + 1];
    bool hostname_truncated;
    char vendor_class[WIRESHARK_DHCP_VENDOR_MAX + 1];
    bool vendor_class_truncated;
    uint8_t client_identifier[WIRESHARK_DHCP_CLIENT_ID_MAX];
    uint8_t client_identifier_len;
    bool client_identifier_truncated;
    bool options_ended;
} wireshark_dhcp_facts_t;

/* Phase 1 contract; implementation remains under nearby_dhcp_parse(). */
wireshark_lan_parse_result_t wireshark_dhcp_parse(
    const uint8_t *message,
    size_t message_len,
    wireshark_dhcp_facts_t *out);

#ifdef __cplusplus
}
#endif
