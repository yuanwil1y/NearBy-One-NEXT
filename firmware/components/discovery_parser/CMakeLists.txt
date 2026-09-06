idf_component_register(
    SRCS
        "ble_ad_parser.c"
        "ble_native_adapter.c"
    INCLUDE_DIRS "include"
    REQUIRES native_handoff bt
)
