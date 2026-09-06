#pragma once

#include "nearby_wifi_discovery.h"
#include "native_handoff.h"

#ifdef __cplusplus
extern "C" {
#endif

void nearby_wifi_normalize_active_record(const wifi_ap_record_t *record,
                                         nearby_wifi_active_facts_t *out);

nearby_wifi_parse_result_t nearby_wifi_normalize_passive_record(
    const nearby_wifi_rx_record_t *record,
    nearby_wifi_passive_normalized_t *out);

#ifdef __cplusplus
}
#endif
