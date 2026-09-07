#include "ha_discovery.h"
#include "ha_recognition.h"
#include "kismet_ble.h"
#include "kismet_dedup.h"
#include "kismet_i154.h"
#include "kismet_lan.h"
#include "kismet_wifi.h"
#include "nearby_ble_discovery.h"
#include "nearby_ble_scan.h"
#include "nearby_i154_discovery.h"
#include "nearby_i154_scan.h"
#include "nearby_lan_discovery.h"
#include "nearby_lan_scan.h"
#include "nearby_matter_discovery.h"
#include "nearby_transport_dedup.h"
#include "nearby_wifi_discovery.h"
#include "nearby_wifi_scan.h"
#include "nearby_zigbee_discovery.h"
#include "wireshark_80211.h"
#include "wireshark_ble.h"
#include "wireshark_dhcp.h"
#include "wireshark_i154.h"
#include "wireshark_mdns.h"
#include "wireshark_ssdp.h"
#include "wireshark_zigbee.h"

_Static_assert(sizeof(nearby_wifi_mgmt_facts_t) ==
                   sizeof(wireshark_80211_mgmt_facts_t),
               "802.11 ownership alias changed layout");
_Static_assert(sizeof(nearby_ble_discovery_facts_t) ==
                   sizeof(wireshark_ble_discovery_facts_t),
               "BLE ownership alias changed layout");
_Static_assert(sizeof(nearby_i154_mac_facts_t) ==
                   sizeof(wireshark_i154_mac_facts_t),
               "802.15.4 ownership alias changed layout");
_Static_assert(sizeof(nearby_zigbee_discovery_facts_t) ==
                   sizeof(wireshark_zigbee_discovery_facts_t),
               "Zigbee ownership alias changed layout");

_Static_assert(sizeof(nearby_wifi_passive_scan_config_t) ==
                   sizeof(kismet_wifi_passive_scan_config_t),
               "Wi-Fi scan ownership alias changed layout");
_Static_assert(sizeof(nearby_ble_scan_config_t) ==
                   sizeof(kismet_ble_scan_config_t),
               "BLE scan ownership alias changed layout");
_Static_assert(sizeof(nearby_i154_scan_config_t) ==
                   sizeof(kismet_i154_scan_config_t),
               "802.15.4 scan ownership alias changed layout");
_Static_assert(sizeof(nearby_scan_dedup_t) == sizeof(kismet_dedup_t),
               "dedup ownership alias changed layout");
_Static_assert(sizeof(nearby_matter_discovery_facts_t) ==
                   sizeof(ha_matter_discovery_facts_t),
               "Matter ownership alias changed layout");
_Static_assert(sizeof(ha_match_result_t) == sizeof(nearby_db_match_result_t),
               "recognition compatibility changed layout");

_Static_assert(NEARBY_WIFI_PARSE_OK == WIRESHARK_80211_PARSE_OK,
               "802.11 result compatibility changed");
_Static_assert(NEARBY_BLE_PARSE_PARTIAL == WIRESHARK_BLE_PARSE_PARTIAL,
               "BLE result compatibility changed");
_Static_assert(NEARBY_I154_PARSE_SECURED == WIRESHARK_I154_PARSE_SECURED,
               "802.15.4 result compatibility changed");
_Static_assert(NEARBY_ZIGBEE_PARSE_UNSUPPORTED ==
                   WIRESHARK_ZIGBEE_PARSE_UNSUPPORTED,
               "Zigbee result compatibility changed");
_Static_assert(NEARBY_MATTER_PARSE_NOT_MATTER ==
                   HA_MATTER_DISCOVERY_NOT_MATTER,
               "Matter result compatibility changed");
_Static_assert(HA_MATCH_AMBIGUOUS == NEARBY_DB_MATCH_AMBIGUOUS,
               "recognition ambiguity semantics changed");

typedef esp_err_t (*kismet_mdns_discover_fn_t)(
    const kismet_mdns_scan_config_t *,
    kismet_mdns_scan_stats_t *);
typedef esp_err_t (*kismet_ssdp_discover_fn_t)(
    const kismet_ssdp_scan_config_t *,
    kismet_ssdp_scan_stats_t *);
typedef esp_err_t (*kismet_dhcp_observe_fn_t)(
    const kismet_dhcp_scan_config_t *,
    kismet_dhcp_scan_stats_t *);

_Static_assert(_Generic(&kismet_mdns_discover,
                        kismet_mdns_discover_fn_t: 1,
                        default: 0),
               "mDNS Kismet public contract changed");
_Static_assert(_Generic(&kismet_ssdp_discover,
                        kismet_ssdp_discover_fn_t: 1,
                        default: 0),
               "SSDP Kismet public contract changed");
_Static_assert(_Generic(&kismet_dhcp_observe,
                        kismet_dhcp_observe_fn_t: 1,
                        default: 0),
               "DHCP Kismet public contract changed");

int main(void)
{
    return 0;
}
