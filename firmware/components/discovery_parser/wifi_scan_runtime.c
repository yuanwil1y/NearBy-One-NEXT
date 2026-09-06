#include "nearby_wifi_scan.h"

#include <string.h>

#include "esp_wifi.h"
#include "freertos/task.h"
#include "nearby_wifi_native_adapter.h"
#include "native_handoff.h"
#include "radio_runtime.h"
#include "scan_session.h"

#define WIFI_PASSIVE_BATCH_MAX 12u

static wifi_ap_record_t s_active_records[NEARBY_WIFI_ACTIVE_RECORD_MAX];

static void remember_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

esp_err_t nearby_wifi_active_scan_run(
    const nearby_wifi_active_scan_config_t *config,
    nearby_wifi_active_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(stats, 0, sizeof(*stats));
    memset(s_active_records, 0, sizeof(s_active_records));

    uint16_t count = NEARBY_WIFI_ACTIVE_RECORD_MAX;
    esp_err_t first_error = nearby_wifi_active_scan(s_active_records, &count,
                                                    config->show_hidden);
    if (first_error == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            nearby_wifi_active_facts_t facts;
            nearby_wifi_normalize_active_record(&s_active_records[i], &facts);
            ++stats->records;
            if (config->dedup != NULL &&
                !nearby_wifi_active_dedup_should_emit(config->dedup, &facts)) {
                ++stats->duplicate_records;
                continue;
            }
            if (config->observation_cb != NULL) {
                const esp_err_t err = config->observation_cb(&facts,
                                                             config->observation_ctx);
                if (err != ESP_OK) {
                    remember_error(err, &first_error);
                    break;
                }
                ++stats->emitted_records;
            }
        }
    }
    remember_error(nearby_wifi_driver_deinit(), &first_error);
    return first_error;
}

static void discard_ready_records(void)
{
    for (;;) {
        nearby_wifi_rx_record_t *record = NULL;
        if (nearby_wifi_rx_take(&record, 0) != ESP_OK) {
            break;
        }
        (void)nearby_wifi_rx_release(record);
    }
}

static esp_err_t consume_passive_batch(
    const nearby_wifi_passive_scan_config_t *config,
    nearby_wifi_passive_scan_stats_t *stats,
    bool *consumed_any)
{
    *consumed_any = false;
    for (unsigned n = 0; n < WIFI_PASSIVE_BATCH_MAX; ++n) {
        nearby_wifi_rx_record_t *record = NULL;
        if (nearby_wifi_rx_take(&record, 0) != ESP_OK) {
            break;
        }
        *consumed_any = true;
        ++stats->raw_records;

        nearby_wifi_passive_normalized_t normalized;
        const nearby_wifi_parse_result_t parsed =
            nearby_wifi_normalize_passive_record(record, &normalized);
        esp_err_t callback_err = ESP_OK;
        if (parsed == NEARBY_WIFI_PARSE_ERR_MALFORMED ||
            parsed == NEARBY_WIFI_PARSE_ERR_ARG) {
            ++stats->malformed_records;
        } else if (parsed == NEARBY_WIFI_PARSE_UNSUPPORTED) {
            ++stats->unsupported_records;
        } else {
            if (parsed == NEARBY_WIFI_PARSE_PARTIAL) {
                ++stats->partial_records;
            }
            bool emit = true;
            if (config->dedup != NULL) {
                emit = nearby_wifi_passive_dedup_should_emit(config->dedup,
                                                             &normalized);
                if (!emit) {
                    ++stats->duplicate_records;
                }
            }
            if (emit && config->observation_cb != NULL) {
                callback_err = config->observation_cb(&normalized, parsed,
                                                      config->observation_ctx);
                if (callback_err == ESP_OK) {
                    ++stats->emitted_records;
                }
            }
        }

        const esp_err_t release_err = nearby_wifi_rx_release(record);
        if (release_err != ESP_OK) {
            return release_err;
        }
        if (callback_err != ESP_OK) {
            return callback_err;
        }
    }
    return ESP_OK;
}

static esp_err_t drain_passive_records(
    const nearby_wifi_passive_scan_config_t *config,
    nearby_wifi_passive_scan_stats_t *stats)
{
    bool consumed = false;
    esp_err_t err = ESP_OK;
    do {
        consumed = false;
        err = consume_passive_batch(config, stats, &consumed);
    } while (err == ESP_OK && consumed);
    return err;
}

esp_err_t nearby_wifi_passive_scan_run(
    const nearby_wifi_passive_scan_config_t *config,
    nearby_wifi_passive_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL || config->dwell_ms == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t first = config->first_channel == 0u ? 1u : config->first_channel;
    uint8_t last = config->last_channel == 0u ? 13u : config->last_channel;
    if (first < 1u || last > 14u || first > last) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(stats, 0, sizeof(*stats));

    esp_err_t first_error = nearby_wifi_driver_init();
    if (first_error != ESP_OK) {
        return first_error;
    }
    stats->drops_before = nearby_wifi_rx_drop_count();

    TickType_t dwell_ticks = pdMS_TO_TICKS(config->dwell_ms);
    if (dwell_ticks == 0) dwell_ticks = 1;
    TickType_t poll_ticks = config->idle_poll_ticks;
    if (poll_ticks == 0) poll_ticks = 1;

    for (uint8_t channel = first; channel <= last; ++channel) {
        esp_err_t err = nearby_wifi_promiscuous_start(channel,
                                                      WIFI_PROMIS_FILTER_MASK_MGMT);
        if (err != ESP_OK) {
            remember_error(err, &first_error);
            break;
        }
        const TickType_t start = xTaskGetTickCount();
        while ((TickType_t)(xTaskGetTickCount() - start) < dwell_ticks) {
            bool consumed = false;
            err = consume_passive_batch(config, stats, &consumed);
            if (err != ESP_OK) {
                remember_error(err, &first_error);
                break;
            }
            if (!consumed) {
                const TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - start);
                const TickType_t remaining = elapsed < dwell_ticks
                                                 ? dwell_ticks - elapsed : 0;
                if (remaining == 0) break;
                vTaskDelay(poll_ticks < remaining ? poll_ticks : remaining);
            }
        }

        remember_error(nearby_wifi_promiscuous_stop(), &first_error);
        if (first_error == ESP_OK) {
            remember_error(drain_passive_records(config, stats), &first_error);
        } else {
            discard_ready_records();
        }
        if (first_error != ESP_OK) {
            break;
        }
        ++stats->channels_completed;
        if (channel == UINT8_MAX) break;
    }

    remember_error(nearby_wifi_driver_deinit(), &first_error);
    stats->drops_after = nearby_wifi_rx_drop_count();
    return first_error;
}
