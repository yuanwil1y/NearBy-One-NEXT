#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kismet_dedup.h"
#include "nearby_ble_discovery.h"
#include "nearby_lan_discovery.h"
#include "nearby_wifi_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Kismet owns scan-generation dedup state. */
#define NEARBY_DEDUP_SLOT_COUNT KISMET_DEDUP_SLOT_COUNT
typedef kismet_dedup_slot_t nearby_dedup_slot_t;
typedef kismet_dedup_table_t nearby_dedup_table_t;
typedef kismet_dedup_t nearby_scan_dedup_t;

/* Legacy symbols retained until the Phase 3 acquisition migration. */
void nearby_scan_dedup_reset(nearby_scan_dedup_t *state, uint32_t generation);

bool nearby_ble_dedup_should_emit(
    nearby_scan_dedup_t *state,
    const nearby_ble_normalized_t *observation);
bool nearby_mdns_dedup_should_emit(
    nearby_scan_dedup_t *state,
    const nearby_mdns_service_t *service);
bool nearby_ssdp_dedup_should_emit(
    nearby_scan_dedup_t *state,
    const nearby_ssdp_facts_t *facts);
bool nearby_dhcp_dedup_should_emit(
    nearby_scan_dedup_t *state,
    const nearby_dhcp_facts_t *facts);
bool nearby_wifi_active_dedup_should_emit(
    nearby_scan_dedup_t *state,
    const nearby_wifi_active_facts_t *facts);
bool nearby_wifi_passive_dedup_should_emit(
    nearby_scan_dedup_t *state,
    const nearby_wifi_passive_normalized_t *observation);

#ifdef __cplusplus
}
#endif
