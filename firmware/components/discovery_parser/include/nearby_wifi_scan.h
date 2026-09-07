#pragma once

#include "kismet_wifi.h"
#include "nearby_transport_dedup.h"
#include "nearby_wifi_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 3 compatibility aliases. Kismet owns finite Wi-Fi acquisition. */
#define NEARBY_WIFI_ACTIVE_RECORD_MAX KISMET_WIFI_ACTIVE_RECORD_MAX
typedef kismet_wifi_active_cb_t nearby_wifi_active_cb_t;
typedef kismet_wifi_passive_cb_t nearby_wifi_passive_cb_t;
typedef kismet_wifi_active_scan_config_t nearby_wifi_active_scan_config_t;
typedef kismet_wifi_active_scan_stats_t nearby_wifi_active_scan_stats_t;
typedef kismet_wifi_passive_scan_config_t nearby_wifi_passive_scan_config_t;
typedef kismet_wifi_passive_scan_stats_t nearby_wifi_passive_scan_stats_t;

/* Legacy link symbols forward to the Kismet implementation. */
esp_err_t nearby_wifi_active_scan_run(const nearby_wifi_active_scan_config_t *config,
                                      nearby_wifi_active_scan_stats_t *stats);
esp_err_t nearby_wifi_passive_scan_run(const nearby_wifi_passive_scan_config_t *config,
                                       nearby_wifi_passive_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
