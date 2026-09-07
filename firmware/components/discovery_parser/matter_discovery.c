#include "nearby_matter_discovery.h"

nearby_matter_parse_result_t nearby_matter_from_ble(
    const nearby_ble_normalized_t *ble,
    nearby_matter_discovery_facts_t *out)
{
    return ha_matter_from_ble(ble, out);
}

nearby_matter_parse_result_t nearby_matter_from_mdns(
    const nearby_mdns_service_t *service,
    nearby_matter_discovery_facts_t *out)
{
    return ha_matter_from_mdns(service, out);
}
