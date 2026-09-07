#pragma once

#include "kismet_i154.h"
#include "nearby_i154_discovery.h"
#include "nearby_zigbee_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 3 compatibility aliases. Kismet owns finite 802.15.4 acquisition. */
typedef kismet_i154_observation_cb_t nearby_i154_observation_cb_t;
typedef kismet_i154_scan_config_t nearby_i154_scan_config_t;
typedef kismet_i154_scan_stats_t nearby_i154_scan_stats_t;

/* Legacy link symbol forwards to the Kismet implementation. */
esp_err_t nearby_i154_scan_run(const nearby_i154_scan_config_t *config,
                               nearby_i154_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
