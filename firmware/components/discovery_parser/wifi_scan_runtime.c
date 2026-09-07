#include "nearby_wifi_scan.h"

esp_err_t nearby_wifi_active_scan_run(
    const nearby_wifi_active_scan_config_t *config,
    nearby_wifi_active_scan_stats_t *stats)
{
    return kismet_wifi_scan_active(config, stats);
}

esp_err_t nearby_wifi_passive_scan_run(
    const nearby_wifi_passive_scan_config_t *config,
    nearby_wifi_passive_scan_stats_t *stats)
{
    return kismet_wifi_scan_passive(config, stats);
}
