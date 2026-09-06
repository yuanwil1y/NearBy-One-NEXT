#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nearby_ble_discovery.h"
#include "nearby_lan_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_MATTER_PARSE_OK = 0,
    NEARBY_MATTER_PARSE_PARTIAL = 1,
    NEARBY_MATTER_PARSE_NOT_MATTER = 2,
    NEARBY_MATTER_PARSE_ERR_ARG = -1,
    NEARBY_MATTER_PARSE_ERR_MALFORMED = -2,
} nearby_matter_parse_result_t;

typedef enum {
    NEARBY_MATTER_SOURCE_BLE = 1,
    NEARBY_MATTER_SOURCE_MDNS = 2,
} nearby_matter_source_t;

typedef struct {
    nearby_matter_source_t source;
    bool has_discriminator;
    uint16_t discriminator;
    bool has_vendor_product;
    uint16_t vendor_id;
    uint16_t product_id;

    bool has_advertisement_version;
    uint8_t advertisement_version;
    bool has_opcode;
    uint8_t opcode;
    bool has_additional_data_flags;
    uint8_t additional_data_flags;

    bool has_commissioning_mode;
    uint8_t commissioning_mode;
    bool has_device_type;
    uint32_t device_type;
} nearby_matter_discovery_facts_t;

nearby_matter_parse_result_t nearby_matter_from_ble(
    const nearby_ble_normalized_t *ble,
    nearby_matter_discovery_facts_t *out);

nearby_matter_parse_result_t nearby_matter_from_mdns(
    const nearby_mdns_service_t *service,
    nearby_matter_discovery_facts_t *out);

#ifdef __cplusplus
}
#endif
