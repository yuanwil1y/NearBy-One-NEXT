#pragma once

#include "kismet_ble.h"
#include "nearby_ble_discovery.h"
#include "nearby_transport_dedup.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 3 compatibility aliases. Kismet owns finite BLE acquisition. */
typedef kismet_ble_observation_cb_t nearby_ble_observation_cb_t;
typedef kismet_ble_scan_config_t nearby_ble_scan_config_t;
typedef kismet_ble_scan_stats_t nearby_ble_scan_stats_t;

/* Legacy link symbol forwards to the Kismet implementation. */
esp_err_t nearby_ble_scan_run(const nearby_ble_scan_config_t *config,
                              nearby_ble_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
