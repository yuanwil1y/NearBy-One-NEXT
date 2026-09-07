#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "kismet_types.h"
#include "wireshark_80211.h"
#include "wireshark_ble.h"
#include "wireshark_dhcp.h"
#include "wireshark_mdns.h"
#include "wireshark_ssdp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 contracts; implementations remain in discovery_parser until Phase 3. */
void kismet_dedup_reset(kismet_dedup_t *state, uint32_t generation);

bool kismet_ble_dedup_should_emit(
    kismet_dedup_t *state,
    const wireshark_ble_observation_t *observation);
bool kismet_mdns_dedup_should_emit(
    kismet_dedup_t *state,
    const wireshark_mdns_service_t *service);
bool kismet_ssdp_dedup_should_emit(
    kismet_dedup_t *state,
    const wireshark_ssdp_facts_t *facts);
bool kismet_dhcp_dedup_should_emit(
    kismet_dedup_t *state,
    const wireshark_dhcp_facts_t *facts);
bool kismet_wifi_active_dedup_should_emit(
    kismet_dedup_t *state,
    const kismet_wifi_active_facts_t *facts);
bool kismet_wifi_passive_dedup_should_emit(
    kismet_dedup_t *state,
    const wireshark_80211_passive_observation_t *observation);

#ifdef __cplusplus
}
#endif
