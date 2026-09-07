#pragma once

#include <stdbool.h>

#include "nearby_recognition_db.h"
#include "wireshark_ble.h"
#include "wireshark_dhcp.h"
#include "wireshark_mdns.h"
#include "wireshark_ssdp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Phase 1 keeps the existing bounded .nbdb runtime underneath the HA public
 * contract. These aliases preserve ABI and fail-closed matching semantics while
 * Phase 2 migrates the adapter implementation.
 */
typedef nearby_db_t ha_recognition_db_t;
typedef nearby_db_result_t ha_recognition_result_t;
typedef nearby_db_match_workspace_t ha_match_workspace_t;
typedef nearby_db_match_status_t ha_match_status_t;
typedef nearby_db_match_result_t ha_match_result_t;

#define HA_MATCH_NOT_FOUND NEARBY_DB_MATCH_NOT_FOUND
#define HA_MATCHED NEARBY_DB_MATCHED
#define HA_MATCH_AMBIGUOUS NEARBY_DB_MATCH_AMBIGUOUS

ha_recognition_result_t ha_match_ble(
    ha_recognition_db_t *db,
    const wireshark_ble_observation_t *observation,
    ha_match_workspace_t *workspace,
    ha_match_result_t *out_result);

ha_recognition_result_t ha_match_zeroconf(
    ha_recognition_db_t *db,
    const wireshark_mdns_service_t *service,
    ha_match_workspace_t *workspace,
    ha_match_result_t *out_result);

ha_recognition_result_t ha_match_ssdp(
    ha_recognition_db_t *db,
    const wireshark_ssdp_facts_t *facts,
    ha_match_workspace_t *workspace,
    ha_match_result_t *out_result);

ha_recognition_result_t ha_match_dhcp(
    ha_recognition_db_t *db,
    const wireshark_dhcp_facts_t *facts,
    ha_match_workspace_t *workspace,
    ha_match_result_t *out_result);

bool ha_match_result_is_usable(const ha_match_result_t *result);

#ifdef __cplusplus
}
#endif
