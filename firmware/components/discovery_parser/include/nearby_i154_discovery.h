#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wireshark_i154.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Wireshark owns IEEE 802.15.4 MAC facts. */
#define NEARBY_I154_ADDR_MAX WIRESHARK_I154_ADDR_MAX

typedef wireshark_i154_parse_result_t nearby_i154_parse_result_t;
#define NEARBY_I154_PARSE_OK WIRESHARK_I154_PARSE_OK
#define NEARBY_I154_PARSE_SECURED WIRESHARK_I154_PARSE_SECURED
#define NEARBY_I154_PARSE_UNSUPPORTED WIRESHARK_I154_PARSE_UNSUPPORTED
#define NEARBY_I154_PARSE_PARTIAL WIRESHARK_I154_PARSE_PARTIAL
#define NEARBY_I154_PARSE_ERR_ARG WIRESHARK_I154_PARSE_ERR_ARG
#define NEARBY_I154_PARSE_ERR_MALFORMED WIRESHARK_I154_PARSE_ERR_MALFORMED

typedef wireshark_i154_mac_facts_t nearby_i154_mac_facts_t;
typedef wireshark_i154_native_meta_t nearby_i154_native_meta_t;
typedef wireshark_i154_observation_t nearby_i154_normalized_t;

/* Legacy symbol retained until the Phase 2 implementation migration. */
nearby_i154_parse_result_t nearby_i154_parse_mac(
    const uint8_t *psdu,
    size_t psdu_len,
    nearby_i154_mac_facts_t *out);

#ifdef __cplusplus
}
#endif
