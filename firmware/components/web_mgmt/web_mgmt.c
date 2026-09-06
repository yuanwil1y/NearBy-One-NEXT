#include "web_mgmt.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "radio_runtime.h"
#include "scan_session.h"

#define WEB_SCAN_MAX_APS       16
#define WEB_FORM_MAX_BYTES     256
#define WEB_UPLOAD_CHUNK_BYTES 2048
#define WEB_STA_CONNECT_TIMEOUT_US (20LL * 1000LL * 1000LL)

static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static nearby_web_mgmt_status_t s_status;
static nearby_db_validation_hooks_t s_db_hooks;
static bool s_db_hooks_set;
static wifi_ap_record_t s_scan_records[WEB_SCAN_MAX_APS];
static volatile nearby_web_mgmt_sta_state_t s_sta_state = NEARBY_WEB_STA_DISCONNECTED;
static int64_t s_sta_connect_started_us;
static esp_event_handler_instance_t s_sta_disconnect_handler;
static esp_event_handler_instance_t s_sta_got_ip_handler;
static bool s_sta_handlers_registered;
static volatile bool s_ignore_disconnect_once;

static esp_err_t send_plain(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t send_status_plain(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    return send_plain(req, text);
}

static void format_ipv4(esp_netif_t *netif, char out[16])
{
    out[0] = '\0';
    if (netif == NULL) {
        return;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
        snprintf(out, 16, IPSTR, IP2STR(&info.ip));
    }
}

static size_t ssid_to_hex(const uint8_t *ssid, char out[65])
{
    static const char hex[] = "0123456789abcdef";
    size_t len = strnlen((const char *)ssid, 32);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hex[(ssid[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[ssid[i] & 0x0f];
    }
    out[len * 2] = '\0';
    return len;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static esp_err_t form_decode(const char *input, char *output, size_t output_size)
{
    if (input == NULL || output == NULL || output_size == 0) return ESP_ERR_INVALID_ARG;
    size_t oi = 0;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        if (oi + 1 >= output_size) return ESP_ERR_INVALID_SIZE;
        if (input[i] == '+') {
            output[oi++] = ' ';
        } else if (input[i] == '%' && input[i + 1] && input[i + 2]) {
            int hi = hex_nibble(input[i + 1]);
            int lo = hex_nibble(input[i + 2]);
            if (hi < 0 || lo < 0) return ESP_ERR_INVALID_ARG;
            output[oi++] = (char)((hi << 4) | lo);
            i += 2;
        } else {
            output[oi++] = input[i];
        }
    }
    output[oi] = '\0';
    return ESP_OK;
}

static const char *sta_state_name(nearby_web_mgmt_sta_state_t state)
{
    switch (state) {
    case NEARBY_WEB_STA_CONNECTING: return "connecting";
    case NEARBY_WEB_STA_CONNECTED: return "connected";
    case NEARBY_WEB_STA_FAILED: return "failed";
    case NEARBY_WEB_STA_DISCONNECTED:
    default: return "disconnected";
    }
}

static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_sta_state = NEARBY_WEB_STA_CONNECTED;
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_ignore_disconnect_once) {
            s_ignore_disconnect_once = false;
            return;
        }
        if (s_sta_state == NEARBY_WEB_STA_CONNECTING) {
            s_sta_state = NEARBY_WEB_STA_FAILED;
        } else if (s_sta_state == NEARBY_WEB_STA_CONNECTED) {
            s_sta_state = NEARBY_WEB_STA_DISCONNECTED;
        }
    }
}

static esp_err_t register_sta_handlers(void)
{
    if (s_sta_handlers_registered) return ESP_OK;

    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT,
                                                         WIFI_EVENT_STA_DISCONNECTED,
                                                         sta_event_handler,
                                                         NULL,
                                                         &s_sta_disconnect_handler);
    if (err != ESP_OK) return err;

    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              sta_event_handler,
                                              NULL,
                                              &s_sta_got_ip_handler);
    if (err != ESP_OK) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT,
                                                    WIFI_EVENT_STA_DISCONNECTED,
                                                    s_sta_disconnect_handler);
        return err;
    }

    s_sta_handlers_registered = true;
    return ESP_OK;
}

static void unregister_sta_handlers(void)
{
    if (!s_sta_handlers_registered) return;

    (void)esp_event_handler_instance_unregister(WIFI_EVENT,
                                                WIFI_EVENT_STA_DISCONNECTED,
                                                s_sta_disconnect_handler);
    (void)esp_event_handler_instance_unregister(IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                s_sta_got_ip_handler);
    s_sta_handlers_registered = false;
}

static bool sta_runtime_active(void)
{
    return s_sta_state == NEARBY_WEB_STA_CONNECTING ||
           s_sta_state == NEARBY_WEB_STA_CONNECTED;
}

static esp_err_t status_handler(httpd_req_t *req)
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

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    uint16_t count = WEB_SCAN_MAX_APS;
    esp_err_t err = nearby_wifi_portal_scan(s_scan_records, &count, true);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi scan failed");

    httpd_resp_set_type(req, "application/json");
    if (httpd_resp_sendstr_chunk(req, "[") != ESP_OK) return ESP_FAIL;
    for (uint16_t i = 0; i < count; ++i) {
        char ssid_hex[65];
        (void)ssid_to_hex(s_scan_records[i].ssid, ssid_hex);
        char item[160];
        snprintf(item, sizeof(item),
                 "%s{\"ssid_hex\":\"%s\",\"rssi\":%d,\"channel\":%u,\"auth\":%d}",
                 i == 0 ? "" : ",",
                 ssid_hex,
                 s_scan_records[i].rssi,
                 s_scan_records[i].primary,
                 s_scan_records[i].authmode);
        if (httpd_resp_sendstr_chunk(req, item) != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(req, "]") != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t recv_complete_body(httpd_req_t *req, char *buffer, size_t capacity)
{
    if (req->content_len >= capacity) return ESP_ERR_INVALID_SIZE;
    size_t received = 0;
    while (received < req->content_len) {
        int rc = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (rc == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (rc <= 0) return ESP_FAIL;
        received += (size_t)rc;
    }
    buffer[received] = '\0';
    return ESP_OK;
}

static esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    char body[WEB_FORM_MAX_BYTES];
    esp_err_t err = recv_complete_body(req, body, sizeof(body));
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid form body");

    char raw_ssid[97] = {0};
    char raw_password[193] = {0};
    char ssid[33] = {0};
    char password[65] = {0};
    if (httpd_query_key_value(body, "ssid", raw_ssid, sizeof(raw_ssid)) != ESP_OK ||
        httpd_query_key_value(body, "password", raw_password, sizeof(raw_password)) != ESP_OK ||
        form_decode(raw_ssid, ssid, sizeof(ssid)) != ESP_OK ||
        form_decode(raw_password, password, sizeof(password)) != ESP_OK || ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid/password invalid");
    }

    wifi_config_t config = {0};
    const size_t ssid_len = strlen(ssid);
    const size_t password_len = strlen(password);
    if (ssid_len > sizeof(config.sta.ssid) || password_len > sizeof(config.sta.password)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid/password too long");
    }
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, password_len);

    wifi_ap_record_t current_ap;
    if (esp_wifi_sta_get_ap_info(&current_ap) == ESP_OK) {
        s_ignore_disconnect_once = true;
        s_sta_state = NEARBY_WEB_STA_DISCONNECTED;
        (void)nearby_wifi_sta_disconnect();
    }

    err = nearby_wifi_set_sta_config(&config);
    if (err != ESP_OK) {
        s_sta_state = NEARBY_WEB_STA_FAILED;
        s_sta_connect_started_us = 0;
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "connect start failed");
    }

    s_sta_state = NEARBY_WEB_STA_CONNECTING;
    s_sta_connect_started_us = esp_timer_get_time();
    err = nearby_wifi_sta_connect();
    if (err != ESP_OK) {
        s_sta_state = NEARBY_WEB_STA_FAILED;
        s_sta_connect_started_us = 0;
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "connect start failed");
    }

    return send_status_plain(req, "202 Accepted", "connecting");
}

static esp_err_t db_preflight_result_handler(httpd_req_t *req)
{
    char body[64];
    char safe[8] = {0};
    esp_err_t err = recv_complete_body(req, body, sizeof(body));
    if (err != ESP_OK ||
        httpd_query_key_value(body, "safe_to_format", safe, sizeof(safe)) != ESP_OK ||
        (strcmp(safe, "1") != 0 && strcmp(safe, "0") != 0)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "safe_to_format=1|0 required");
    }

    /* Transport only: C owns how this verdict was derived. */
    err = nearby_db_storage_set_preflight_safe_to_format(strcmp(safe, "1") == 0);
    if (err != ESP_OK) {
        return send_status_plain(req, "409 Conflict", "preflight result not accepted in current state");
    }
    return send_plain(req, strcmp(safe, "1") == 0 ? "authorized" : "cleared");
}

static esp_err_t db_format_handler(httpd_req_t *req)
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

static esp_err_t db_upload_handler(httpd_req_t *req)
{
    if (!s_db_hooks_set || req->content_len == 0) {
        return send_status_plain(req, "503 Service Unavailable", "DB validator not registered or empty upload");
    }

    esp_err_t err = nearby_db_upload_begin(req->content_len, &s_db_hooks);
    if (err != ESP_OK) return send_status_plain(req, "409 Conflict", "DB upload not prepared");

    uint8_t chunk[WEB_UPLOAD_CHUNK_BYTES];
    size_t remaining = req->content_len;
    while (remaining > 0) {
        size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        int rc = httpd_req_recv(req, (char *)chunk, want);
        if (rc == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (rc <= 0) {
            (void)nearby_db_upload_cancel();
            return ESP_FAIL;
        }
        err = nearby_db_upload_write(chunk, (size_t)rc);
        if (err != ESP_OK) {
            (void)nearby_db_upload_cancel();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD stream write failed");
        }
        remaining -= (size_t)rc;
    }

    err = nearby_db_upload_finish();
    if (err != ESP_OK) {
        (void)nearby_db_upload_cancel();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "DB validation/promotion failed");
    }
    return send_plain(req, "promoted");
}

static esp_err_t register_api_handlers(void)
{
    const httpd_uri_t handlers[] = {
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = wifi_scan_handler},
        {.uri = "/api/wifi/connect", .method = HTTP_POST, .handler = wifi_connect_handler},
        {.uri = "/api/db/preflight-result", .method = HTTP_POST, .handler = db_preflight_result_handler},
        {.uri = "/api/db/format", .method = HTTP_POST, .handler = db_format_handler},
        {.uri = "/api/db/upload", .method = HTTP_POST, .handler = db_upload_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(s_server, &handlers[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t nearby_web_mgmt_set_db_validation_hooks(const nearby_db_validation_hooks_t *hooks)
{
    if (hooks == NULL || hooks->on_finish == NULL || s_server != NULL) return ESP_ERR_INVALID_STATE;
    s_db_hooks = *hooks;
    s_db_hooks_set = true;
    return ESP_OK;
}

esp_err_t nearby_web_mgmt_start(void)
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
    s_status.ssid[0] = ' ';
    s_status.ap_ipv4[0] = ' ';

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
    if (ip[0] != ' ') {
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

    if (status.sta_ipv4[0] != ' ') {
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

httpd_handle_t nearby_web_mgmt_httpd(void)
{
    return s_server;
}
