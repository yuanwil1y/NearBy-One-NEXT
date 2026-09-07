#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wireshark_ble.h"
#include "wireshark_mdns.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HA_MATTER_DISCOVERY_OK = 0,
    HA_MATTER_DISCOVERY_PARTIAL = 1,
    HA_MATTER_DISCOVERY_NOT_MATTER = 2,
    HA_MATTER_DISCOVERY_ERR_ARG = -1,
    HA_MATTER_DISCOVERY_ERR_MALFORMED = -2,
} ha_matter_discovery_result_t;

typedef enum {
    HA_MATTER_SOURCE_BLE = 1,
    HA_MATTER_SOURCE_MDNS = 2,
} ha_matter_source_t;

typedef struct {
    ha_matter_source_t source;
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
} ha_matter_discovery_facts_t;

/* Phase 1 contracts; implementations remain under nearby_* until Phase 2. */
ha_matter_discovery_result_t ha_matter_from_ble(
    const wireshark_ble_observation_t *ble,
    ha_matter_discovery_facts_t *out);

ha_matter_discovery_result_t ha_matter_from_mdns(
    const wireshark_mdns_service_t *service,
    ha_matter_discovery_facts_t *out);

#ifdef __cplusplus
}
#endif
