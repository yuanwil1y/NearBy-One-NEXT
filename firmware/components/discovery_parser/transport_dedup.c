#include "nearby_transport_dedup.h"

void nearby_scan_dedup_reset(nearby_scan_dedup_t *state, uint32_t generation)
{
    kismet_dedup_reset(state, generation);
}

bool nearby_ble_dedup_should_emit(nearby_scan_dedup_t *state, const nearby_ble_normalized_t *observation)
{ return kismet_ble_dedup_should_emit(state, observation); }
bool nearby_mdns_dedup_should_emit(nearby_scan_dedup_t *state, const nearby_mdns_service_t *service)
{ return kismet_mdns_dedup_should_emit(state, service); }
bool nearby_ssdp_dedup_should_emit(nearby_scan_dedup_t *state, const nearby_ssdp_facts_t *facts)
{ return kismet_ssdp_dedup_should_emit(state, facts); }
bool nearby_dhcp_dedup_should_emit(nearby_scan_dedup_t *state, const nearby_dhcp_facts_t *facts)
{ return kismet_dhcp_dedup_should_emit(state, facts); }
bool nearby_wifi_active_dedup_should_emit(nearby_scan_dedup_t *state, const nearby_wifi_active_facts_t *facts)
{ return kismet_wifi_active_dedup_should_emit(state, facts); }
bool nearby_wifi_passive_dedup_should_emit(nearby_scan_dedup_t *state, const nearby_wifi_passive_normalized_t *observation)
{ return kismet_wifi_passive_dedup_should_emit(state, observation); }
