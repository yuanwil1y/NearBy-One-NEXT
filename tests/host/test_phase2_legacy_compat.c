#include "nearby_ble_discovery.h"
#include "nearby_i154_discovery.h"
#include "nearby_lan_discovery.h"
#include "nearby_matter_discovery.h"
#include "nearby_recognition_adapter.h"
#include "nearby_wifi_discovery.h"
#include "nearby_zigbee_discovery.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    nearby_ble_discovery_facts_t ble;
    assert(nearby_ble_parse_advertisement(NULL, 0u, false, &ble) ==
           NEARBY_BLE_PARSE_OK);

    nearby_wifi_mgmt_facts_t wifi;
    assert(nearby_wifi_parse_management_frame(NULL, 0u, false, &wifi) ==
           NEARBY_WIFI_PARSE_ERR_MALFORMED);

    nearby_mdns_facts_t mdns;
    assert(nearby_mdns_parse(NULL, 0u, &mdns) ==
           NEARBY_LAN_PARSE_ERR_MALFORMED);

    nearby_ssdp_facts_t ssdp;
    assert(nearby_ssdp_parse(NULL, 0u, &ssdp) ==
           NEARBY_LAN_PARSE_ERR_MALFORMED);

    nearby_dhcp_facts_t dhcp;
    assert(nearby_dhcp_parse(NULL, 0u, &dhcp) ==
           NEARBY_LAN_PARSE_ERR_MALFORMED);

    nearby_i154_mac_facts_t mac;
    assert(nearby_i154_parse_mac(NULL, 0u, &mac) ==
           NEARBY_I154_PARSE_ERR_MALFORMED);

    nearby_zigbee_discovery_facts_t zigbee;
    assert(nearby_zigbee_parse_from_mac(NULL, 0u, NULL, &zigbee) ==
           NEARBY_ZIGBEE_PARSE_ERR_ARG);

    nearby_ble_normalized_t observation;
    memset(&observation, 0, sizeof(observation));
    nearby_matter_discovery_facts_t matter;
    assert(nearby_matter_from_ble(&observation, &matter) ==
           NEARBY_MATTER_PARSE_NOT_MATTER);

    nearby_mdns_service_t service;
    memset(&service, 0, sizeof(service));
    assert(nearby_matter_from_mdns(&service, &matter) ==
           NEARBY_MATTER_PARSE_NOT_MATTER);

    assert(nearby_recognition_match_ble(NULL, NULL, NULL, NULL) ==
           NEARBY_DB_ERR_ARG);
    assert(!nearby_recognition_result_is_usable(NULL));

    puts("phase2 legacy parser/HA compatibility: OK");
    return 0;
}
