#include "nearby_ble_scan.h"

#include <string.h>

#include "freertos/task.h"
#include "nearby_ble_native_adapter.h"
#include "native_handoff.h"
#include "radio_runtime.h"
#include "scan_session.h"

#define BLE_DEFAULT_SYNC_TIMEOUT_MS 3000u
#define BLE_CONSUME_BATCH_MAX 16u

static void remember_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

static esp_err_t emit_legacy(const nearby_ble_scan_config_t *config,
                             nearby_ble_scan_stats_t *stats,
                             nearby_ble_disc_record_t *record)
{
    nearby_ble_normalized_t normalized;
    const nearby_ble_parse_result_t parsed =
        nearby_ble_normalize_legacy_record(record, &normalized);
    ++stats->legacy_records;

    esp_err_t callback_err = ESP_OK;
    if (parsed == NEARBY_BLE_PARSE_ERR_MALFORMED ||
        parsed == NEARBY_BLE_PARSE_ERR_ARG) {
        ++stats->malformed_records;
    } else {
        if (parsed == NEARBY_BLE_PARSE_PARTIAL) {
            ++stats->partial_records;
        }
        if (config->observation_cb != NULL) {
            callback_err = config->observation_cb(&normalized, parsed,
                                                  config->observation_ctx);
            if (callback_err == ESP_OK) {
                ++stats->emitted_records;
            }
        }
    }

    const esp_err_t release_err = nearby_ble_disc_release(record);
    return release_err != ESP_OK ? release_err : callback_err;
}

#if MYNEWT_VAL(BLE_EXT_ADV)
static esp_err_t emit_extended(const nearby_ble_scan_config_t *config,
                               nearby_ble_scan_stats_t *stats,
                               nearby_ble_ext_disc_record_t *record)
{
    nearby_ble_normalized_t normalized;
    const nearby_ble_parse_result_t parsed =
        nearby_ble_normalize_extended_record(record, &normalized);
    ++stats->extended_records;

    esp_err_t callback_err = ESP_OK;
    if (parsed == NEARBY_BLE_PARSE_ERR_MALFORMED ||
        parsed == NEARBY_BLE_PARSE_ERR_ARG) {
        ++stats->malformed_records;
    } else {
        if (parsed == NEARBY_BLE_PARSE_PARTIAL) {
            ++stats->partial_records;
        }
        if (config->observation_cb != NULL) {
            callback_err = config->observation_cb(&normalized, parsed,
                                                  config->observation_ctx);
            if (callback_err == ESP_OK) {
                ++stats->emitted_records;
            }
        }
    }

    const esp_err_t release_err = nearby_ble_ext_disc_release(record);
    return release_err != ESP_OK ? release_err : callback_err;
}
#endif

static esp_err_t consume_batch(const nearby_ble_scan_config_t *config,
                               nearby_ble_scan_stats_t *stats,
                               bool *consumed_any)
{
    *consumed_any = false;
    for (unsigned n = 0; n < BLE_CONSUME_BATCH_MAX; ++n) {
        bool consumed = false;
#if MYNEWT_VAL(BLE_EXT_ADV)
        nearby_ble_ext_disc_record_t *ext = NULL;
        if (nearby_ble_ext_disc_take(&ext, 0) == ESP_OK) {
            consumed = true;
            *consumed_any = true;
            esp_err_t err = emit_extended(config, stats, ext);
            if (err != ESP_OK) {
                return err;
            }
        }
#endif
        nearby_ble_disc_record_t *legacy = NULL;
        if (nearby_ble_disc_take(&legacy, 0) == ESP_OK) {
            consumed = true;
            *consumed_any = true;
            esp_err_t err = emit_legacy(config, stats, legacy);
            if (err != ESP_OK) {
                return err;
            }
        }
        if (!consumed) {
            break;
        }
    }
    return ESP_OK;
}

static void discard_queued_records(void)
{
    /* Queue capacities are fixed by D; this loop is bounded by the number queued. */
#if MYNEWT_VAL(BLE_EXT_ADV)
    for (;;) {
        nearby_ble_ext_disc_record_t *ext = NULL;
        if (nearby_ble_ext_disc_take(&ext, 0) != ESP_OK) {
            break;
        }
        (void)nearby_ble_ext_disc_release(ext);
    }
#endif
    for (;;) {
        nearby_ble_disc_record_t *legacy = NULL;
        if (nearby_ble_disc_take(&legacy, 0) != ESP_OK) {
            break;
        }
        (void)nearby_ble_disc_release(legacy);
    }
}

esp_err_t nearby_ble_scan_run(const nearby_ble_scan_config_t *config,
                              nearby_ble_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL || config->duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(stats, 0, sizeof(*stats));

    TickType_t sync_timeout = config->sync_timeout_ticks;
    if (sync_timeout == 0) {
        sync_timeout = pdMS_TO_TICKS(BLE_DEFAULT_SYNC_TIMEOUT_MS);
        if (sync_timeout == 0) {
            sync_timeout = 1;
        }
    }

    esp_err_t first_error = nearby_nimble_driver_init(sync_timeout);
    if (first_error != ESP_OK) {
        return first_error;
    }

    stats->legacy_drops_before = nearby_ble_rx_drop_count();
#if MYNEWT_VAL(BLE_EXT_ADV)
    stats->extended_drops_before = nearby_ble_ext_rx_drop_count();
#endif

    esp_err_t err = nearby_nimble_scan_start(config->duration_ms,
                                             config->passive);
    if (err != ESP_OK) {
        first_error = err;
        goto cleanup;
    }

    TickType_t duration_ticks = pdMS_TO_TICKS(config->duration_ms);
    if (duration_ticks == 0) {
        duration_ticks = 1;
    }
    TickType_t poll_ticks = config->idle_poll_ticks;
    if (poll_ticks == 0) {
        poll_ticks = 1;
    }
    const TickType_t start = xTaskGetTickCount();

    while ((TickType_t)(xTaskGetTickCount() - start) < duration_ticks) {
        bool consumed_any = false;
        err = consume_batch(config, stats, &consumed_any);
        if (err != ESP_OK) {
            remember_error(err, &first_error);
            break;
        }
        if (!consumed_any) {
            const TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - start);
            const TickType_t remaining = elapsed < duration_ticks
                                             ? duration_ticks - elapsed : 0;
            if (remaining == 0) {
                break;
            }
            vTaskDelay(poll_ticks < remaining ? poll_ticks : remaining);
        }
    }

    remember_error(nearby_nimble_scan_stop(), &first_error);

    if (first_error == ESP_OK) {
        /* Capture reports already queued when stop/auto-complete became terminal. */
        bool consumed_any = false;
        do {
            consumed_any = false;
            err = consume_batch(config, stats, &consumed_any);
            remember_error(err, &first_error);
        } while (consumed_any && err == ESP_OK);
    } else {
        /* On sink/ownership failure, return all D slots without invoking callbacks. */
        discard_queued_records();
    }

cleanup:
    remember_error(nearby_nimble_driver_deinit(), &first_error);
    stats->legacy_drops_after = nearby_ble_rx_drop_count();
#if MYNEWT_VAL(BLE_EXT_ADV)
    stats->extended_drops_after = nearby_ble_ext_rx_drop_count();
#endif
    return first_error;
}
