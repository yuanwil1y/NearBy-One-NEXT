#include "kismet_wifi.h"

#include <string.h>

#include "esp_wifi.h"
#include "freertos/task.h"
#include "kismet_dedup.h"
#include "native_handoff.h"
#include "radio_runtime.h"
#include "scan_session.h"

#define WIFI_PASSIVE_BATCH_MAX 12u

static wifi_ap_record_t s_active_records[KISMET_WIFI_ACTIVE_RECORD_MAX];

static void remember_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

static void normalize_active_record(const wifi_ap_record_t *record,
                                    kismet_wifi_active_facts_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (record == NULL) {
        return;
    }
    memcpy(out->bssid, record->bssid, sizeof(out->bssid));
    size_t ssid_len = 0;
    while (ssid_len < KISMET_WIFI_SSID_MAX && record->ssid[ssid_len] != 0u) {
        ++ssid_len;
    }
    if (ssid_len > 0u) {
        memcpy(out->ssid, record->ssid, ssid_len);
    }
    out->ssid_len = (uint8_t)ssid_len;
    out->hidden_ssid = ssid_len == 0u;
    out->primary_channel = record->primary;
    out->rssi = record->rssi;
    out->authmode = (int)record->authmode;
}

esp_err_t kismet_wifi_scan_active(
    const kismet_wifi_active_scan_config_t *config,
    kismet_wifi_active_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(stats, 0, sizeof(*stats));
    memset(s_active_records, 0, sizeof(s_active_records));

    /* Preserve existing lifecycle behavior; Phase 4 owns broader cleanup. */
    const bool driver_was_started = nearby_wifi_driver_is_started();
    uint16_t count = KISMET_WIFI_ACTIVE_RECORD_MAX;
    esp_err_t first_error = nearby_wifi_active_scan(s_active_records, &count,
                                                    config->show_hidden);
    if (first_error == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            kismet_wifi_active_facts_t facts;
            normalize_active_record(&s_active_records[i], &facts);
            ++stats->records;
            if (config->dedup != NULL &&
                !kismet_wifi_active_dedup_should_emit(config->dedup, &facts)) {
                ++stats->duplicate_records;
                continue;
            }
            if (config->observation_cb != NULL) {
                const esp_err_t err = config->observation_cb(
                    &facts, config->observation_ctx);
                if (err != ESP_OK) {
                    remember_error(err, &first_error);
                    break;
                }
                ++stats->emitted_records;
            }
        }
    }
    if (!driver_was_started) {
        remember_error(nearby_wifi_driver_deinit(), &first_error);
    }
    return first_error;
}

static wireshark_80211_parse_result_t normalize_passive_record(
    const nearby_wifi_rx_record_t *record,
    wireshark_80211_passive_observation_t *out)
{
    if (record == NULL || out == NULL) {
        return WIRESHARK_80211_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->meta.rssi = record->rx_ctrl.rssi;
    out->meta.channel = record->rx_ctrl.channel;
    out->meta.original_len = record->original_len;
    out->meta.captured_len = record->captured_len;
    out->meta.source_truncated = record->truncated;
    if (record->type != WIFI_PKT_MGMT) {
        return WIRESHARK_80211_PARSE_UNSUPPORTED;
    }
    return wireshark_80211_parse_mgmt(record->payload,
                                     record->captured_len,
                                     record->truncated,
                                     &out->facts);
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
    const kismet_wifi_passive_scan_config_t *config,
    kismet_wifi_passive_scan_stats_t *stats,
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

        wireshark_80211_passive_observation_t normalized;
        const wireshark_80211_parse_result_t parsed =
            normalize_passive_record(record, &normalized);
        esp_err_t callback_err = ESP_OK;
        if (parsed == WIRESHARK_80211_PARSE_ERR_MALFORMED ||
            parsed == WIRESHARK_80211_PARSE_ERR_ARG) {
            ++stats->malformed_records;
        } else if (parsed == WIRESHARK_80211_PARSE_UNSUPPORTED) {
            ++stats->unsupported_records;
        } else {
            if (parsed == WIRESHARK_80211_PARSE_PARTIAL) {
                ++stats->partial_records;
            }
            bool emit = true;
            if (config->dedup != NULL) {
                emit = kismet_wifi_passive_dedup_should_emit(config->dedup,
                                                             &normalized);
                if (!emit) {
                    ++stats->duplicate_records;
                }
            }
            if (emit && config->observation_cb != NULL) {
                callback_err = config->observation_cb(
                    &normalized, parsed, config->observation_ctx);
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
    const kismet_wifi_passive_scan_config_t *config,
    kismet_wifi_passive_scan_stats_t *stats)
{
    bool consumed = false;
    esp_err_t err = ESP_OK;
    do {
        consumed = false;
        err = consume_passive_batch(config, stats, &consumed);
    } while (err == ESP_OK && consumed);
    return err;
}

esp_err_t kismet_wifi_scan_passive(
    const kismet_wifi_passive_scan_config_t *config,
    kismet_wifi_passive_scan_stats_t *stats)
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
        esp_err_t err = nearby_wifi_promiscuous_start(
            channel, WIFI_PROMIS_FILTER_MASK_MGMT);
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
                const TickType_t elapsed_ticks =
                    (TickType_t)(xTaskGetTickCount() - start);
                const TickType_t remaining = elapsed_ticks < dwell_ticks
                                                 ? dwell_ticks - elapsed_ticks
                                                 : 0;
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
