idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nearby_board scan_session native_handoff radio_runtime esp_system esp_timer
)
