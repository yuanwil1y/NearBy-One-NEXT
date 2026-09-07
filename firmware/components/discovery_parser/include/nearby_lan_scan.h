#pragma once

#include "kismet_lan.h"
#include "nearby_lan_discovery.h"
#include "nearby_transport_dedup.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 1 compatibility aliases. Kismet owns finite LAN acquisition types. */
#define NEARBY_LAN_RX_MAX KISMET_LAN_RX_MAX
#define NEARBY_MDNS_DISCOVERED_TYPE_MAX KISMET_MDNS_DISCOVERED_TYPE_MAX

typedef kismet_mdns_observation_cb_t nearby_mdns_observation_cb_t;
typedef kismet_ssdp_observation_cb_t nearby_ssdp_observation_cb_t;
typedef kismet_dhcp_observation_cb_t nearby_dhcp_observation_cb_t;

typedef kismet_mdns_scan_config_t nearby_zeroconf_scan_config_t;
typedef kismet_mdns_scan_stats_t nearby_zeroconf_scan_stats_t;
typedef kismet_ssdp_scan_config_t nearby_ssdp_scan_config_t;
typedef kismet_ssdp_scan_stats_t nearby_ssdp_scan_stats_t;
typedef kismet_dhcp_scan_config_t nearby_dhcp_scan_config_t;
typedef kismet_dhcp_scan_stats_t nearby_dhcp_scan_stats_t;

/* Legacy symbols retained until the Phase 3 acquisition migration. */
bool nearby_lan_scan_prerequisite_ready(void);

esp_err_t nearby_zeroconf_scan_run(
    const nearby_zeroconf_scan_config_t *config,
    nearby_zeroconf_scan_stats_t *stats);
esp_err_t nearby_ssdp_scan_run(
    const nearby_ssdp_scan_config_t *config,
    nearby_ssdp_scan_stats_t *stats);
esp_err_t nearby_dhcp_scan_run(
    const nearby_dhcp_scan_config_t *config,
    nearby_dhcp_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
