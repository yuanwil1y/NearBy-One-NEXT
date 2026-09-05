idf_component_register(
    SRCS "wifi_runtime.c" "nimble_runtime.c" "i154_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event bt ieee802154 nvs_flash freertos native_handoff scan_session
)
