idf_component_register(
    SRCS
        "matter_discovery.c"
        "recognition_adapter.c"
    INCLUDE_DIRS "include"
    REQUIRES wireshark recognition_db ha_core
)
