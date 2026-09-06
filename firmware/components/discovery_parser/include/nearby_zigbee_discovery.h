#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nearby_i154_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_ZIGBEE_MAX_CLUSTERS 12u
#define NEARBY_ZIGBEE_MAX_ENDPOINTS 16u
#define NEARBY_ZIGBEE_MANUFACTURER_MAX 32u
#define NEARBY_ZIGBEE_MODEL_MAX 40u

typedef enum {
    NEARBY_ZIGBEE_PARSE_OK = 0,
    NEARBY_ZIGBEE_PARSE_SECURED = 1,
    NEARBY_ZIGBEE_PARSE_UNSUPPORTED = 2,
    NEARBY_ZIGBEE_PARSE_PARTIAL = 3,
    NEARBY_ZIGBEE_PARSE_ERR_ARG = -1,
    NEARBY_ZIGBEE_PARSE_ERR_MALFORMED = -2,
} nearby_zigbee_parse_result_t;

typedef struct {
    uint8_t nwk_frame_type;
    uint8_t nwk_protocol_version;
    bool nwk_security;
    uint16_t destination_nwk;
    uint16_t source_nwk;
    uint8_t radius;
    uint8_t nwk_sequence;
    bool has_destination_ieee;
    uint8_t destination_ieee[8];
    bool has_source_ieee;
    uint8_t source_ieee[8];

    bool has_aps;
    uint16_t profile_id;
    uint16_t cluster_id;
    uint8_t destination_endpoint;
    uint8_t source_endpoint;
    uint8_t aps_counter;

    bool has_device_announce;
    uint16_t announced_nwk;
    uint8_t announced_ieee[8];
    uint8_t announced_capability;

    bool has_node_descriptor;
    uint16_t manufacturer_code;

    bool has_simple_descriptor;
    uint8_t descriptor_endpoint;
    uint16_t descriptor_profile_id;
    uint16_t descriptor_device_id;
    uint8_t descriptor_device_version;
    uint16_t input_clusters[NEARBY_ZIGBEE_MAX_CLUSTERS];
    uint8_t input_cluster_count;
    bool input_cluster_overflow;
    uint16_t output_clusters[NEARBY_ZIGBEE_MAX_CLUSTERS];
    uint8_t output_cluster_count;
    bool output_cluster_overflow;

    uint8_t active_endpoints[NEARBY_ZIGBEE_MAX_ENDPOINTS];
    uint8_t active_endpoint_count;
    bool active_endpoint_overflow;

    bool has_manufacturer;
    char manufacturer[NEARBY_ZIGBEE_MANUFACTURER_MAX + 1u];
    bool manufacturer_truncated;
    bool has_model;
    char model[NEARBY_ZIGBEE_MODEL_MAX + 1u];
    bool model_truncated;
} nearby_zigbee_discovery_facts_t;

nearby_zigbee_parse_result_t nearby_zigbee_parse_from_mac(
    const uint8_t *psdu,
    size_t psdu_len,
    const nearby_i154_mac_facts_t *mac,
    nearby_zigbee_discovery_facts_t *out);

#ifdef __cplusplus
}
#endif
