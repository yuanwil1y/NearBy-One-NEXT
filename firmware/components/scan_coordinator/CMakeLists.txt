idf_component_register(
    SRCS "scan_coordinator.c" "scan_default_runners.c"
    INCLUDE_DIRS "include"
    REQUIRES scan_session radio_runtime discovery_parser kismet
)
