#pragma once

#include <stdbool.h>

#include "nearby_ble_discovery.h"
#include "nearby_lan_discovery.h"
#include "nearby_recognition_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin B -> C adapters. These functions only translate B's already-normalized
 * bounded facts into Agent C's frozen typed matcher API. They do not perform
 * recognition, candidate selection, precedence, or fallback matching.
 */
nearby_db_result_t nearby_recognition_match_ble(
    nearby_db_t *db,
    const nearby_ble_normalized_t *observation,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

nearby_db_result_t nearby_recognition_match_zeroconf(
    nearby_db_t *db,
    const nearby_mdns_service_t *service,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

nearby_db_result_t nearby_recognition_match_ssdp(
    nearby_db_t *db,
    const nearby_ssdp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

nearby_db_result_t nearby_recognition_match_dhcp(
    nearby_db_t *db,
    const nearby_dhcp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result);

/* Only MATCHED is a usable recognition result. AMBIGUOUS fails closed. */
bool nearby_recognition_result_is_usable(const nearby_db_match_result_t *result);

#ifdef __cplusplus
}
#endif
