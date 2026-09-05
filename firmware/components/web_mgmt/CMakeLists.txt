idf_component_register(
    SRCS "web_mgmt.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_wifi esp_netif radio_runtime scan_session db_storage
)
