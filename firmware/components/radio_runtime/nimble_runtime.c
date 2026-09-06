#include "radio_runtime.h"
#include "radio_runtime_internal.h"

#include <limits.h>

#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "native_handoff.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "scan_session.h"

static StaticSemaphore_t s_sync_sem_storage;
static SemaphoreHandle_t s_sync_sem;
static bool s_initialized;
static bool s_ready;
static bool s_scanning;
static uint8_t s_own_addr_type;

static void latch_scan_fatal(esp_err_t reason)
{
    if (reason != ESP_OK) {
        (void)scan_session_mark_fatal(reason);
    }
}

static esp_err_t release_nimble_phase_if_owned(void)
{
    if (nearby_radio_phase_current() == NEARBY_RADIO_PHASE_NIMBLE) {
        return nearby_radio_phase_release(NEARBY_RADIO_PHASE_NIMBLE);
    }
    return ESP_OK;
}

static void nearby_nimble_on_reset(int reason)
{
    (void)reason;
    s_ready = false;
    s_scanning = false;
}

static void nearby_nimble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    s_ready = rc == 0;
    if (s_sync_sem != NULL) xSemaphoreGive(s_sync_sem);
}

static void nearby_nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static int nearby_nimble_runtime_gap_event(struct ble_gap_event *event, void *arg)
{
    if (event != NULL && event->type == BLE_GAP_EVENT_DISC_COMPLETE) s_scanning = false;
    return nearby_nimble_gap_event_cb(event, arg);
}

esp_err_t nearby_nimble_driver_init(TickType_t sync_timeout_ticks)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_ready) return ESP_OK;

    esp_err_t err = nearby_radio_phase_claim(NEARBY_RADIO_PHASE_NIMBLE);
    if (err != ESP_OK) return err;

    err = nearby_ble_handoff_init();
    if (err != ESP_OK) goto fail_phase;

    if (s_sync_sem == NULL) {
        s_sync_sem = xSemaphoreCreateBinaryStatic(&s_sync_sem_storage);
        if (s_sync_sem == NULL) {
            err = ESP_ERR_NO_MEM;
            goto fail_phase;
        }
    }
    while (xSemaphoreTake(s_sync_sem, 0) == pdTRUE) {
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        /* Hardware layer never erases NVS as a recovery side effect. */
        goto fail_phase;
    }

    err = nimble_port_init();
    if (err != ESP_OK) goto fail_phase;
    s_initialized = true;
    s_ready = false;
    s_scanning = false;

    ble_hs_cfg.reset_cb = nearby_nimble_on_reset;
    ble_hs_cfg.sync_cb = nearby_nimble_on_sync;
    nimble_port_freertos_init(nearby_nimble_host_task);

    if (xSemaphoreTake(s_sync_sem, sync_timeout_ticks) != pdTRUE || !s_ready) {
        err = nearby_nimble_driver_deinit();
        return err == ESP_OK ? ESP_ERR_TIMEOUT : err;
    }
    return ESP_OK;

fail_phase: {
        esp_err_t phase_err = release_nimble_phase_if_owned();
        if (phase_err != ESP_OK) {
            latch_scan_fatal(phase_err);
            return phase_err;
        }
        return err;
    }
}

esp_err_t nearby_nimble_scan_start(uint32_t duration_ms, bool passive)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (!s_ready || s_scanning) return ESP_ERR_INVALID_STATE;

    int rc;
#if MYNEWT_VAL(BLE_EXT_ADV)
    /*
     * Extended discovery duration is in 10 ms units and uint16_t. A zero
     * duration means scan continuously until nearby_nimble_scan_stop().
     * Scan both uncoded and coded PHYs; B receives native EXT_DISC records and
     * decides how to interpret them. D does not reassemble chained reports.
     */
    uint16_t duration_units = 0;
    if (duration_ms != 0) {
        const uint32_t rounded_units = duration_ms / 10U + (duration_ms % 10U != 0U ? 1U : 0U);
        if (rounded_units > UINT16_MAX) return ESP_ERR_INVALID_ARG;
        duration_units = (uint16_t)rounded_units;
    }

    struct ble_gap_ext_disc_params params = {0};
    params.passive = passive ? 1 : 0;

    rc = ble_gap_ext_disc(s_own_addr_type,
                          duration_units,
                          0,
                          0,
                          0,
                          0,
                          &params,
                          &params,
                          nearby_nimble_runtime_gap_event,
                          NULL);
#else
    struct ble_gap_disc_params params = {0};
    params.passive = passive ? 1 : 0;
    params.filter_duplicates = 0;
    params.filter_policy = 0;
    params.limited = 0;

    rc = ble_gap_disc(s_own_addr_type, (int32_t)duration_ms, &params,
                      nearby_nimble_runtime_gap_event, NULL);
#endif
    if (rc != 0) return ESP_FAIL;
    s_scanning = true;
    return ESP_OK;
}

esp_err_t nearby_nimble_scan_stop(void)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (!s_scanning) return ESP_OK;

    int rc = ble_gap_disc_cancel();
    if (rc != 0) return ESP_FAIL;
    s_scanning = false;
    return ESP_OK;
}

esp_err_t nearby_nimble_driver_deinit(void)
{
    if (scan_session_require_scan_owner() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (!s_initialized) {
        s_ready = false;
        s_scanning = false;
        esp_err_t phase_err = release_nimble_phase_if_owned();
        if (phase_err != ESP_OK) latch_scan_fatal(phase_err);
        return phase_err;
    }

    esp_err_t first_err = ESP_OK;
    if (s_scanning) first_err = nearby_nimble_scan_stop();

    int rc = nimble_port_stop();
    if (rc != 0) {
        /* Host task may still own controller resources; do not release gate. */
        latch_scan_fatal(ESP_FAIL);
        return first_err != ESP_OK ? first_err : ESP_FAIL;
    }

    rc = nimble_port_deinit();
    if (rc != ESP_OK) {
        latch_scan_fatal(rc);
        return first_err != ESP_OK ? first_err : rc;
    }

    s_initialized = false;
    s_ready = false;
    s_scanning = false;
    esp_err_t phase_err = nearby_radio_phase_release(NEARBY_RADIO_PHASE_NIMBLE);
    if (phase_err != ESP_OK) latch_scan_fatal(phase_err);
    return first_err != ESP_OK ? first_err : phase_err;
}

bool nearby_nimble_driver_is_ready(void)
{
    return s_ready;
}
