idf_component_register(
    SRCS
        "ble_ad_parser.c"
        "ble_native_adapter.c"
        "ble_scan_runtime.c"
        "mdns_parser.c"
        "ssdp_parser.c"
        "dhcp_parser.c"
    INCLUDE_DIRS "include"
    REQUIRES native_handoff radio_runtime scan_session bt
)
