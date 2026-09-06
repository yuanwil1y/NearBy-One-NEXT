#pragma once

#include "nearby_ble_scan.h"
#include "nearby_i154_scan.h"
#include "nearby_lan_scan.h"
#include "nearby_scan_coordinator.h"
#include "nearby_wifi_scan.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed product scanner wiring. This is deliberately seven concrete slots, not
 * a provider/plugin registry. A configured enabled module always has a runner.
 */
typedef struct {
    const nearby_wifi_active_scan_config_t *wifi_active_config;
    nearby_wifi_active_scan_stats_t *wifi_active_stats;
    const nearby_wifi_passive_scan_config_t *wifi_management_config;
    nearby_wifi_passive_scan_stats_t *wifi_management_stats;

    const nearby_zeroconf_scan_config_t *zeroconf_config;
    nearby_zeroconf_scan_stats_t *zeroconf_stats;
    const nearby_ssdp_scan_config_t *ssdp_config;
    nearby_ssdp_scan_stats_t *ssdp_stats;
    const nearby_dhcp_scan_config_t *dhcp_config;
    nearby_dhcp_scan_stats_t *dhcp_stats;

    const nearby_ble_scan_config_t *ble_config;
    nearby_ble_scan_stats_t *ble_stats;
    const nearby_i154_scan_config_t *i154_config;
    nearby_i154_scan_stats_t *i154_stats;
} nearby_scan_default_context_t;

void nearby_scan_default_runners_init(nearby_scan_default_context_t *context,
                                      nearby_scan_runners_t *out_runners);

#ifdef __cplusplus
}
#endif
