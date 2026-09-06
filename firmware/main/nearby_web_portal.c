#include "nearby_web_portal.h"

#include <stddef.h>
#include <stdint.h>

extern const uint8_t portal_index_start[] asm("_binary_portal_index_html_start");
extern const uint8_t portal_index_end[] asm("_binary_portal_index_html_end");

static esp_err_t portal_index_handler(httpd_req_t *req)
{
    const size_t len = (size_t)(portal_index_end - portal_index_start);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)portal_index_start, (ssize_t)len);
}

esp_err_t nearby_web_portal_register(httpd_handle_t server)
{
    if (server == NULL) return ESP_ERR_INVALID_ARG;
    const httpd_uri_t index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = portal_index_handler,
        .user_ctx = NULL,
    };
    return httpd_register_uri_handler(server, &index);
}
