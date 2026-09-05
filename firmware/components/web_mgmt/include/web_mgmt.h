#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "db_storage.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    char ssid[33];
    char password[65];
    char ap_ipv4[16];
    char sta_ipv4[16];
    bool sta_connected;
    bool db_format_authorized;
} nearby_web_mgmt_status_t;

/** Register C-owned DB validation hooks used by the raw upload endpoint. */
esp_err_t nearby_web_mgmt_set_db_validation_hooks(const nearby_db_validation_hooks_t *hooks);

/**
 * Reserve the product gate, start session-only Wi-Fi in APSTA mode, create a
 * temporary WPA2 SoftAP and start the narrow transport HTTP API.
 */
esp_err_t nearby_web_mgmt_start(void);

/** Stop HTTP/SoftAP/Wi-Fi and release the competing-operation gate. */
esp_err_t nearby_web_mgmt_stop(void);

esp_err_t nearby_web_mgmt_get_status(nearby_web_mgmt_status_t *out_status);

/** Expose the server handle so Agent E can register its static portal routes. */
httpd_handle_t nearby_web_mgmt_httpd(void);

#ifdef __cplusplus
}
#endif
