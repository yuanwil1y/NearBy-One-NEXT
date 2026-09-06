idf_component_register(
    SRCS "native_handoff.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi bt ieee802154 freertos
)
