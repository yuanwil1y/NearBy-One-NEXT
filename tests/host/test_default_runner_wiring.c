#include "nearby_scan_default_runners.h"

#include <assert.h>
#include <stdio.h>

bool nearby_lan_scan_prerequisite_ready(void) { return false; }
esp_err_t nearby_wifi_active_scan_run(const nearby_wifi_active_scan_config_t *c, nearby_wifi_active_scan_stats_t *s) { (void)c; (void)s; return ESP_OK; }
esp_err_t nearby_wifi_passive_scan_run(const nearby_wifi_passive_scan_config_t *c, nearby_wifi_passive_scan_stats_t *s) { (void)c; (void)s; return ESP_OK; }
esp_err_t nearby_zeroconf_scan_run(const nearby_zeroconf_scan_config_t *c, nearby_zeroconf_scan_stats_t *s) { (void)c; (void)s; return ESP_OK; }
esp_err_t nearby_ssdp_scan_run(const nearby_ssdp_scan_config_t *c, nearby_ssdp_scan_stats_t *s) { (void)c; (void)s; return ESP_OK; }
esp_err_t nearby_dhcp_scan_run(const nearby_dhcp_scan_config_t *c, nearby_dhcp_scan_stats_t *s) { (void)c; (void)s; return ESP_OK; }
esp_err_t nearby_ble_scan_run(const nearby_ble_scan_config_t *c, nearby_ble_scan_stats_t *s) { (void)c; (void)s; return ESP_OK; }
esp_err_t nearby_i154_scan_run(const nearby_i154_scan_config_t *c, nearby_i154_scan_stats_t *s) { (void)c; (void)s; return ESP_OK; }

int main(void)
{
    nearby_scan_default_context_t context = {0};
    nearby_scan_runners_t runners = {0};
    nearby_scan_default_runners_init(&context, &runners);
    assert(runners.ctx == &context);
    assert(runners.wifi_active != NULL);
    assert(runners.wifi_management != NULL);
    assert(runners.zeroconf != NULL);
    assert(runners.ssdp != NULL);
    assert(runners.dhcp != NULL);
    assert(runners.ble != NULL);
    assert(runners.ieee802154 != NULL);
    puts("default runner wiring host tests: OK");
    return 0;
}
