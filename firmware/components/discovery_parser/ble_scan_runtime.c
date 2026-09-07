#include "nearby_ble_scan.h"

esp_err_t nearby_ble_scan_run(const nearby_ble_scan_config_t *config,
                              nearby_ble_scan_stats_t *stats)
{
    return kismet_ble_scan(config, stats);
}
