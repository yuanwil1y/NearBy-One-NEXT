#include "radio_runtime.h"

#include "scan_session.h"

static void remember_first_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

esp_err_t nearby_radio_scan_cleanup_all(void)
{
    if (scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t first_error = ESP_OK;

    /* All three paths are deliberately idempotent. Calling every teardown
     * catches partial-init/reset states where a simple "ready" flag is false
     * even though a driver still owns the shared RF phase. */
    remember_first_error(nearby_i154_receive_stop(), &first_error);
    remember_first_error(nearby_nimble_driver_deinit(), &first_error);
    remember_first_error(nearby_wifi_driver_deinit(), &first_error);

    return first_error;
}
