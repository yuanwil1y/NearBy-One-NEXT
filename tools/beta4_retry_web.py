from pathlib import Path
import re


def sub_once(path, pattern, replacement):
    p = Path(path)
    text = p.read_text()
    text2, n = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if n != 1:
        raise RuntimeError(f"{path}: pattern matched {n} times")
    p.write_text(text2)


p = Path("firmware/components/web_mgmt/web_mgmt.c")
s = p.read_text()
s = s.replace('#include <inttypes.h>\n', '').replace('#include "esp_random.h"\n', '')
p.write_text(s)

sub_once("firmware/components/web_mgmt/web_mgmt.c",
         r'(static void unregister_sta_handlers\(void\)\n\{.*?\n\})\n\nstatic esp_err_t status_handler',
         r'''\1

static bool sta_runtime_active(void)
{
    return s_sta_state == NEARBY_WEB_STA_CONNECTING ||
           s_sta_state == NEARBY_WEB_STA_CONNECTED;
}

static esp_err_t status_handler''')

sub_once("firmware/components/web_mgmt/web_mgmt.c",
         r'static esp_err_t status_handler\(httpd_req_t \*req\)\n\{.*?\n\}\n\nstatic esp_err_t wifi_scan_handler',
         r'''static esp_err_t status_handler(httpd_req_t *req)
{
    nearby_web_mgmt_status_t status;
    esp_err_t err = nearby_web_mgmt_get_status(&status);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status failed");

    char response[560];
    snprintf(response, sizeof(response),
             "{\"active\":%s,\"ssid\":\"%s\",\"ap_ipv4\":\"%s\","
             "\"sta_connected\":%s,\"sta_state\":\"%s\",\"sta_ipv4\":\"%s\","
             "\"db_format_authorized\":%s,\"db_format_state\":\"%s\","
             "\"db_format_stage\":\"%s\",\"db_format_error\":%d,"
             "\"db_format_worker_active\":%s}",
             status.active ? "true" : "false",
             status.ssid,
             status.ap_ipv4,
             status.sta_connected ? "true" : "false",
             sta_state_name(status.sta_state),
             status.sta_ipv4,
             status.db_format_authorized ? "true" : "false",
             nearby_db_storage_format_state_name(nearby_db_storage_format_state()),
             nearby_db_storage_format_stage(),
             (int)nearby_db_storage_format_error(),
             nearby_db_storage_format_is_active() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t wifi_scan_handler''')

sub_once("firmware/components/web_mgmt/web_mgmt.c",
         r'static esp_err_t db_format_handler\(httpd_req_t \*req\)\n\{.*?\n\}\n\nstatic esp_err_t db_upload_handler',
         r'''static esp_err_t db_format_handler(httpd_req_t *req)
{
    char query[64] = {0};
    char confirm[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "confirm", confirm, sizeof(confirm)) != ESP_OK ||
        strcmp(confirm, "ERASE") != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "explicit confirm=ERASE required");
    }
    if (!nearby_db_storage_preflight_is_authorized()) {
        return send_status_plain(req, "409 Conflict", "C preflight safe_to_format authorization required");
    }

    esp_err_t err = nearby_db_storage_prepare_whole_sd(true);
    if (err != ESP_OK) {
        char message[96];
        snprintf(message, sizeof(message), "whole-SD format start failed: %s", esp_err_to_name(err));
        return send_status_plain(req, "500 Internal Server Error", message);
    }
    return send_status_plain(req, "202 Accepted", "formatting");
}

static esp_err_t db_upload_handler''')

sub_once("firmware/components/web_mgmt/web_mgmt.c",
         r'esp_err_t nearby_web_mgmt_start\(void\)\n\{.*?\nhttpd_handle_t nearby_web_mgmt_httpd',
         r'''esp_err_t nearby_web_mgmt_start(void)
{
    if (s_server != NULL) return ESP_ERR_INVALID_STATE;

    bool acquired_gate = false;
    if (scan_session_competing_op_is_active()) {
        if (scan_session_require_competing_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    } else {
        esp_err_t gate_err = scan_session_competing_op_try_begin();
        if (gate_err != ESP_OK) return gate_err;
        acquired_gate = true;
    }

    (void)nearby_db_storage_set_preflight_safe_to_format(false);
    const bool had_runtime = nearby_wifi_driver_is_started();
    esp_err_t err = nearby_wifi_driver_init();
    if (err != ESP_OK) goto fail_gate;
    if (!had_runtime) {
        s_sta_state = NEARBY_WEB_STA_DISCONNECTED;
        s_sta_connect_started_us = 0;
        s_ignore_disconnect_once = false;
    }
    err = register_sta_handlers();
    if (err != ESP_OK) goto fail_wifi;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail_wifi;
    }
    err = nearby_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) goto fail_ap;

    uint8_t mac[6];
    err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) goto fail_ap;
    memset(&s_status, 0, sizeof(s_status));
    snprintf(s_status.ssid, sizeof(s_status.ssid), "NearBy-One-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap_config = {0};
    const size_t ap_ssid_len = strlen(s_status.ssid);
    if (ap_ssid_len > sizeof(ap_config.ap.ssid)) {
        err = ESP_ERR_INVALID_SIZE;
        goto fail_ap;
    }
    memcpy(ap_config.ap.ssid, s_status.ssid, ap_ssid_len);
    ap_config.ap.ssid_len = (uint8_t)ap_ssid_len;
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    err = nearby_wifi_set_ap_config(&ap_config);
    if (err != ESP_OK) goto fail_ap;

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_uri_handlers = 10;
    http_config.stack_size = 6144;
    err = httpd_start(&s_server, &http_config);
    if (err != ESP_OK) goto fail_ap;
    err = register_api_handlers();
    if (err != ESP_OK) goto fail_http;

    format_ipv4(s_ap_netif, s_status.ap_ipv4);
    s_status.active = true;
    return ESP_OK;

fail_http:
    (void)httpd_stop(s_server);
    s_server = NULL;
fail_ap:
    if (nearby_wifi_driver_is_started()) (void)nearby_wifi_set_mode(WIFI_MODE_STA);
    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
fail_wifi:
    if (!had_runtime || !sta_runtime_active()) {
        unregister_sta_handlers();
        (void)nearby_wifi_driver_deinit();
    }
fail_gate:
    if (acquired_gate && !sta_runtime_active()) (void)scan_session_competing_op_end();
    memset(&s_status, 0, sizeof(s_status));
    return err;
}

esp_err_t nearby_web_mgmt_stop(void)
{
    if (scan_session_require_competing_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;

    esp_err_t first_err = ESP_OK;
    if (s_server != NULL) {
        esp_err_t err = httpd_stop(s_server);
        if (err != ESP_OK) first_err = err;
        s_server = NULL;
    }
    if (nearby_db_upload_is_active()) (void)nearby_db_upload_cancel();
    (void)nearby_db_storage_set_preflight_safe_to_format(false);

    if (nearby_wifi_driver_is_started()) {
        esp_err_t err = nearby_wifi_set_mode(WIFI_MODE_STA);
        if (first_err == ESP_OK && err != ESP_OK) first_err = err;
    }
    if (s_ap_netif != NULL) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    s_status.active = false;
    s_status.ssid[0] = '\0';
    s_status.ap_ipv4[0] = '\0';

    if (sta_runtime_active() || nearby_db_storage_format_is_active()) {
        return first_err;
    }

    unregister_sta_handlers();
    esp_err_t err = nearby_wifi_driver_deinit();
    if (first_err == ESP_OK && err != ESP_OK) first_err = err;
    err = scan_session_competing_op_end();
    if (first_err == ESP_OK && err != ESP_OK) first_err = err;
    s_sta_state = NEARBY_WEB_STA_DISCONNECTED;
    s_sta_connect_started_us = 0;
    s_ignore_disconnect_once = false;
    return first_err;
}

esp_err_t nearby_web_mgmt_maintenance(void)
{
    if (s_server != NULL || !scan_session_competing_op_is_active()) return ESP_OK;
    if (scan_session_require_competing_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (nearby_db_storage_format_is_active()) return ESP_OK;

    char ip[16];
    format_ipv4(nearby_wifi_sta_netif(), ip);
    if (ip[0] != '\0') {
        s_sta_state = NEARBY_WEB_STA_CONNECTED;
        return ESP_OK;
    }
    if (s_sta_state == NEARBY_WEB_STA_CONNECTING && s_sta_connect_started_us > 0 &&
        esp_timer_get_time() - s_sta_connect_started_us < WEB_STA_CONNECT_TIMEOUT_US) {
        return ESP_OK;
    }
    if (s_sta_state == NEARBY_WEB_STA_CONNECTING) s_sta_state = NEARBY_WEB_STA_FAILED;

    unregister_sta_handlers();
    esp_err_t first_err = nearby_wifi_driver_deinit();
    esp_err_t err = scan_session_competing_op_end();
    if (first_err == ESP_OK && err != ESP_OK) first_err = err;
    s_sta_state = NEARBY_WEB_STA_DISCONNECTED;
    s_sta_connect_started_us = 0;
    s_ignore_disconnect_once = false;
    return first_err;
}

esp_err_t nearby_web_mgmt_get_status(nearby_web_mgmt_status_t *out_status)
{
    if (out_status == NULL) return ESP_ERR_INVALID_ARG;
    nearby_web_mgmt_status_t status = s_status;
    status.active = s_server != NULL;
    format_ipv4(s_ap_netif, status.ap_ipv4);
    format_ipv4(nearby_wifi_sta_netif(), status.sta_ipv4);
    status.db_format_authorized = nearby_db_storage_preflight_is_authorized();

    if (status.sta_ipv4[0] != '\0') {
        s_sta_state = NEARBY_WEB_STA_CONNECTED;
        status.sta_state = NEARBY_WEB_STA_CONNECTED;
    } else if (s_sta_state == NEARBY_WEB_STA_CONNECTING &&
               s_sta_connect_started_us > 0 &&
               esp_timer_get_time() - s_sta_connect_started_us >= WEB_STA_CONNECT_TIMEOUT_US) {
        s_sta_state = NEARBY_WEB_STA_FAILED;
        status.sta_state = NEARBY_WEB_STA_FAILED;
    } else {
        status.sta_state = s_sta_state;
    }
    status.sta_connected = status.sta_state == NEARBY_WEB_STA_CONNECTED;
    *out_status = status;
    return ESP_OK;
}

httpd_handle_t nearby_web_mgmt_httpd''')

Path("firmware/components/web_mgmt/include/web_mgmt.h").write_text(r'''#pragma once

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
''')
