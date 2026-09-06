#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "db_storage.h"
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_WEB_STA_DISCONNECTED = 0,
    NEARBY_WEB_STA_CONNECTING,
    NEARBY_WEB_STA_CONNECTED,
    NEARBY_WEB_STA_FAILED,
} nearby_web_mgmt_sta_state_t;

typedef struct {
    bool active;
    char ssid[33];
    char ap_ipv4[16];
    char sta_ipv4[16];
    bool sta_connected;
    nearby_web_mgmt_sta_state_t sta_state;
    bool db_format_authorized;
} nearby_web_mgmt_status_t;

esp_err_t nearby_web_mgmt_set_db_validation_hooks(const nearby_db_validation_hooks_t *hooks);

/** Start HTTP + an open beta SoftAP while retaining any existing RAM-only STA session. */
esp_err_t nearby_web_mgmt_start(void);

/** Stop only HTTP + SoftAP. An active STA session and RF lease are preserved. */
esp_err_t nearby_web_mgmt_stop(void);

/** Owner-task cleanup after a preserved STA session disconnects or fails. */
esp_err_t nearby_web_mgmt_maintenance(void);

esp_err_t nearby_web_mgmt_get_status(nearby_web_mgmt_status_t *out_status);
httpd_handle_t nearby_web_mgmt_httpd(void);

#ifdef __cplusplus
}
#endif
