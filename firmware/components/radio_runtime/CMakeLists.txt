idf_component_register(
    SRCS "radio_phase.c" "wifi_runtime.c" "nimble_runtime.c" "i154_runtime.c" "radio_cleanup.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event bt ieee802154 nvs_flash freertos native_handoff scan_session
)
