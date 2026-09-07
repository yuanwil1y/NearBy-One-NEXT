#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wireshark_dhcp.h"
#include "wireshark_mdns.h"
#include "wireshark_ssdp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Wireshark owns LAN parser facts. */
typedef wireshark_lan_parse_result_t nearby_lan_parse_result_t;
#define NEARBY_LAN_PARSE_OK WIRESHARK_LAN_PARSE_OK
#define NEARBY_LAN_PARSE_PARTIAL WIRESHARK_LAN_PARSE_PARTIAL
#define NEARBY_LAN_PARSE_ERR_ARG WIRESHARK_LAN_PARSE_ERR_ARG
#define NEARBY_LAN_PARSE_ERR_MALFORMED WIRESHARK_LAN_PARSE_ERR_MALFORMED

#define NEARBY_MDNS_NAME_MAX WIRESHARK_MDNS_NAME_MAX
#define NEARBY_MDNS_MAX_SERVICES WIRESHARK_MDNS_MAX_SERVICES
#define NEARBY_MDNS_MAX_TXT WIRESHARK_MDNS_MAX_TXT
#define NEARBY_MDNS_TXT_KEY_MAX WIRESHARK_MDNS_TXT_KEY_MAX
#define NEARBY_MDNS_TXT_VALUE_MAX WIRESHARK_MDNS_TXT_VALUE_MAX

typedef wireshark_mdns_txt_t nearby_mdns_txt_t;
typedef wireshark_mdns_service_t nearby_mdns_service_t;
typedef wireshark_mdns_facts_t nearby_mdns_facts_t;

#define NEARBY_SSDP_START_LINE_MAX WIRESHARK_SSDP_START_LINE_MAX
#define NEARBY_SSDP_VALUE_MAX WIRESHARK_SSDP_VALUE_MAX
typedef wireshark_ssdp_facts_t nearby_ssdp_facts_t;

#define NEARBY_DHCP_HOSTNAME_MAX WIRESHARK_DHCP_HOSTNAME_MAX
#define NEARBY_DHCP_VENDOR_MAX WIRESHARK_DHCP_VENDOR_MAX
#define NEARBY_DHCP_CLIENT_ID_MAX WIRESHARK_DHCP_CLIENT_ID_MAX
typedef wireshark_dhcp_facts_t nearby_dhcp_facts_t;

/* Legacy symbols retained until the Phase 2 implementation migration. */
nearby_lan_parse_result_t nearby_mdns_parse(
    const uint8_t *message,
    size_t message_len,
    nearby_mdns_facts_t *out);
nearby_lan_parse_result_t nearby_ssdp_parse(
    const uint8_t *message,
    size_t message_len,
    nearby_ssdp_facts_t *out);
nearby_lan_parse_result_t nearby_dhcp_parse(
    const uint8_t *message,
    size_t message_len,
    nearby_dhcp_facts_t *out);

#ifdef __cplusplus
}
#endif
