#include "nearby_i154_scan.h"

esp_err_t nearby_i154_scan_run(const nearby_i154_scan_config_t *config,
                               nearby_i154_scan_stats_t *stats)
{
    return kismet_i154_scan(config, stats);
}
