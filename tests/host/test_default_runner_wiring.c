#include "nearby_scan_default_runners.h"

#include <assert.h>
#include <stdio.h>

static unsigned calls[7];
bool kismet_lan_scan_prerequisite_ready(void) { return true; }
esp_err_t kismet_wifi_scan_active(const kismet_wifi_active_scan_config_t *c, kismet_wifi_active_scan_stats_t *s) { assert(c && s); ++calls[0]; return ESP_OK; }
esp_err_t kismet_wifi_scan_passive(const kismet_wifi_passive_scan_config_t *c, kismet_wifi_passive_scan_stats_t *s) { assert(c && s); ++calls[1]; return ESP_OK; }
esp_err_t kismet_mdns_discover(const kismet_mdns_scan_config_t *c, kismet_mdns_scan_stats_t *s) { assert(c && s); ++calls[2]; return ESP_OK; }
esp_err_t kismet_ssdp_discover(const kismet_ssdp_scan_config_t *c, kismet_ssdp_scan_stats_t *s) { assert(c && s); ++calls[3]; return ESP_OK; }
esp_err_t kismet_dhcp_observe(const kismet_dhcp_scan_config_t *c, kismet_dhcp_scan_stats_t *s) { assert(c && s); ++calls[4]; return ESP_OK; }
esp_err_t kismet_ble_scan(const kismet_ble_scan_config_t *c, kismet_ble_scan_stats_t *s) { assert(c && s); ++calls[5]; return ESP_OK; }
esp_err_t kismet_i154_scan(const kismet_i154_scan_config_t *c, kismet_i154_scan_stats_t *s) { assert(c && s); ++calls[6]; return ESP_OK; }

int main(void)
{
    nearby_wifi_active_scan_config_t a={0}; nearby_wifi_active_scan_stats_t as={0};
    nearby_wifi_passive_scan_config_t p={0}; nearby_wifi_passive_scan_stats_t ps={0};
    nearby_zeroconf_scan_config_t m={0}; nearby_zeroconf_scan_stats_t ms={0};
    nearby_ssdp_scan_config_t s={0}; nearby_ssdp_scan_stats_t ss={0};
    nearby_dhcp_scan_config_t d={0}; nearby_dhcp_scan_stats_t ds={0};
    nearby_ble_scan_config_t b={0}; nearby_ble_scan_stats_t bs={0};
    nearby_i154_scan_config_t i={0}; nearby_i154_scan_stats_t is={0};
    nearby_scan_default_context_t context={.wifi_active_config=&a,.wifi_active_stats=&as,.wifi_management_config=&p,.wifi_management_stats=&ps,.zeroconf_config=&m,.zeroconf_stats=&ms,.ssdp_config=&s,.ssdp_stats=&ss,.dhcp_config=&d,.dhcp_stats=&ds,.ble_config=&b,.ble_stats=&bs,.i154_config=&i,.i154_stats=&is};
    nearby_scan_runners_t runners={0};
    nearby_scan_default_runners_init(&context,&runners);
    assert(runners.ctx==&context);
    assert(runners.wifi_active(runners.ctx).status==NEARBY_SCAN_MODULE_COMPLETE);
    assert(runners.wifi_management(runners.ctx).status==NEARBY_SCAN_MODULE_COMPLETE);
    assert(runners.zeroconf(runners.ctx).status==NEARBY_SCAN_MODULE_COMPLETE);
    assert(runners.ssdp(runners.ctx).status==NEARBY_SCAN_MODULE_COMPLETE);
    assert(runners.dhcp(runners.ctx).status==NEARBY_SCAN_MODULE_COMPLETE);
    assert(runners.ble(runners.ctx).status==NEARBY_SCAN_MODULE_COMPLETE);
    assert(runners.ieee802154(runners.ctx).status==NEARBY_SCAN_MODULE_COMPLETE);
    for (unsigned n=0;n<7;++n) assert(calls[n]==1u);
    puts("default runner Kismet wiring host tests: OK");
    return 0;
}
