#include "nearby_recognition_adapter.h"
#include "ha_recognition.h"

nearby_db_result_t nearby_recognition_match_ble(
    nearby_db_t *db,
    const nearby_ble_normalized_t *observation,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    return ha_match_ble(db, observation, workspace, out_result);
}

nearby_db_result_t nearby_recognition_match_zeroconf(
    nearby_db_t *db,
    const nearby_mdns_service_t *service,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    return ha_match_zeroconf(db, service, workspace, out_result);
}

nearby_db_result_t nearby_recognition_match_ssdp(
    nearby_db_t *db,
    const nearby_ssdp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    return ha_match_ssdp(db, facts, workspace, out_result);
}

nearby_db_result_t nearby_recognition_match_dhcp(
    nearby_db_t *db,
    const nearby_dhcp_facts_t *facts,
    nearby_db_match_workspace_t *workspace,
    nearby_db_match_result_t *out_result)
{
    return ha_match_dhcp(db, facts, workspace, out_result);
}

bool nearby_recognition_result_is_usable(const nearby_db_match_result_t *result)
{
    return ha_match_result_is_usable(result);
}
