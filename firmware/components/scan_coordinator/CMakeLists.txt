idf_component_register(
    SRCS "scan_coordinator.c"
    INCLUDE_DIRS "include"
    REQUIRES scan_session radio_runtime
)
