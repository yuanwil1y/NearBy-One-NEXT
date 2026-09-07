#include "nearby_lan_scan.h"

bool nearby_lan_scan_prerequisite_ready(void)
{
    return kismet_lan_scan_prerequisite_ready();
}

esp_err_t nearby_zeroconf_scan_run(
    const nearby_zeroconf_scan_config_t *config,
    nearby_zeroconf_scan_stats_t *stats)
{
    return kismet_mdns_discover(config, stats);
}

esp_err_t nearby_ssdp_scan_run(
    const nearby_ssdp_scan_config_t *config,
    nearby_ssdp_scan_stats_t *stats)
{
    return kismet_ssdp_discover(config, stats);
}

esp_err_t nearby_dhcp_scan_run(
    const nearby_dhcp_scan_config_t *config,
    nearby_dhcp_scan_stats_t *stats)
{
    return kismet_dhcp_observe(config, stats);
}
