idf_component_register(
    SRCS
        "ble_ad_parser.c"
        "ble_native_adapter.c"
        "ble_scan_runtime.c"
        "mdns_parser.c"
        "ssdp_parser.c"
        "dhcp_parser.c"
        "matcher_keys.c"
        "transport_dedup.c"
        "wifi_management_parser.c"
        "wifi_native_adapter.c"
        "wifi_scan_runtime.c"
        "i154_mac_parser.c"
        "i154_native_adapter.c"
        "i154_scan_runtime.c"
        "zigbee_discovery.c"
        "matter_discovery.c"
    INCLUDE_DIRS "include"
    REQUIRES native_handoff radio_runtime scan_session bt esp_wifi ieee802154 freertos
)
