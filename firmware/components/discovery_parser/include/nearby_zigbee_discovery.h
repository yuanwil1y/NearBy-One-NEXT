#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nearby_i154_discovery.h"
#include "wireshark_zigbee.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Wireshark owns Zigbee discovery facts. */
#define NEARBY_ZIGBEE_MAX_CLUSTERS WIRESHARK_ZIGBEE_MAX_CLUSTERS
#define NEARBY_ZIGBEE_MAX_ENDPOINTS WIRESHARK_ZIGBEE_MAX_ENDPOINTS
#define NEARBY_ZIGBEE_MANUFACTURER_MAX WIRESHARK_ZIGBEE_MANUFACTURER_MAX
#define NEARBY_ZIGBEE_MODEL_MAX WIRESHARK_ZIGBEE_MODEL_MAX

typedef wireshark_zigbee_parse_result_t nearby_zigbee_parse_result_t;
#define NEARBY_ZIGBEE_PARSE_OK WIRESHARK_ZIGBEE_PARSE_OK
#define NEARBY_ZIGBEE_PARSE_SECURED WIRESHARK_ZIGBEE_PARSE_SECURED
#define NEARBY_ZIGBEE_PARSE_UNSUPPORTED WIRESHARK_ZIGBEE_PARSE_UNSUPPORTED
#define NEARBY_ZIGBEE_PARSE_PARTIAL WIRESHARK_ZIGBEE_PARSE_PARTIAL
#define NEARBY_ZIGBEE_PARSE_ERR_ARG WIRESHARK_ZIGBEE_PARSE_ERR_ARG
#define NEARBY_ZIGBEE_PARSE_ERR_MALFORMED WIRESHARK_ZIGBEE_PARSE_ERR_MALFORMED

typedef wireshark_zigbee_discovery_facts_t nearby_zigbee_discovery_facts_t;

/* Legacy symbol retained until the Phase 2 implementation migration. */
nearby_zigbee_parse_result_t nearby_zigbee_parse_from_mac(
    const uint8_t *psdu,
    size_t psdu_len,
    const nearby_i154_mac_facts_t *mac,
    nearby_zigbee_discovery_facts_t *out);

#ifdef __cplusplus
}
#endif
