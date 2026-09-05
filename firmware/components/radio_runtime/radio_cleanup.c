#include "radio_runtime.h"
#include "radio_runtime_internal.h"

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

    /*
     * Always call all three teardown paths. This handles partial-init/reset
     * states where a public "ready" flag is false but D still owns an RF
     * phase. A failed teardown is fatal for the current complete scan: the
     * product gate must remain held until a later retry proves everything is
     * actually down.
     */
    remember_first_error(nearby_i154_receive_stop(), &first_error);
    remember_first_error(nearby_nimble_driver_deinit(), &first_error);
    remember_first_error(nearby_wifi_driver_deinit(), &first_error);

    if (first_error != ESP_OK || nearby_radio_phase_current() != NEARBY_RADIO_PHASE_NONE) {
        const esp_err_t fatal = first_error != ESP_OK ? first_error : ESP_FAIL;
        (void)scan_session_mark_fatal(fatal);
        return fatal;
    }

    /* A subsequent successful retry is the only normal way to clear fatal. */
    return scan_session_recovery_clear_fatal();
}
