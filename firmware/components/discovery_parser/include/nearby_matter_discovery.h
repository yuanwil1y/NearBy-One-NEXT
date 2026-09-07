#pragma once

#include "ha_discovery.h"
#include "nearby_ble_discovery.h"
#include "nearby_lan_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Matter discovery semantics belong to HA. */
typedef ha_matter_discovery_result_t nearby_matter_parse_result_t;
#define NEARBY_MATTER_PARSE_OK HA_MATTER_DISCOVERY_OK
#define NEARBY_MATTER_PARSE_PARTIAL HA_MATTER_DISCOVERY_PARTIAL
#define NEARBY_MATTER_PARSE_NOT_MATTER HA_MATTER_DISCOVERY_NOT_MATTER
#define NEARBY_MATTER_PARSE_ERR_ARG HA_MATTER_DISCOVERY_ERR_ARG
#define NEARBY_MATTER_PARSE_ERR_MALFORMED HA_MATTER_DISCOVERY_ERR_MALFORMED

typedef ha_matter_source_t nearby_matter_source_t;
#define NEARBY_MATTER_SOURCE_BLE HA_MATTER_SOURCE_BLE
#define NEARBY_MATTER_SOURCE_MDNS HA_MATTER_SOURCE_MDNS

typedef ha_matter_discovery_facts_t nearby_matter_discovery_facts_t;

/* Legacy symbols retained until the Phase 2 semantic migration. */
nearby_matter_parse_result_t nearby_matter_from_ble(
    const nearby_ble_normalized_t *ble,
    nearby_matter_discovery_facts_t *out);

nearby_matter_parse_result_t nearby_matter_from_mdns(
    const nearby_mdns_service_t *service,
    nearby_matter_discovery_facts_t *out);

#ifdef __cplusplus
}
#endif
