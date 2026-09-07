#pragma once

#include "kismet_i154.h"
#include "nearby_i154_discovery.h"
#include "nearby_zigbee_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Kismet owns finite 802.15.4 acquisition types. */
typedef kismet_i154_observation_cb_t nearby_i154_observation_cb_t;
typedef kismet_i154_scan_config_t nearby_i154_scan_config_t;
typedef kismet_i154_scan_stats_t nearby_i154_scan_stats_t;

/* Legacy symbol retained until the Phase 3 acquisition migration. */
esp_err_t nearby_i154_scan_run(
    const nearby_i154_scan_config_t *config,
    nearby_i154_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
