#include "kismet_i154.h"

#include <string.h>

#include "freertos/task.h"
#include "native_handoff.h"
#include "radio_runtime.h"
#include "scan_session.h"

#define I154_BATCH_MAX 16u

static void remember_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

static wireshark_i154_parse_result_t normalize_record(
    const nearby_i154_rx_record_t *record,
    wireshark_i154_observation_t *out)
{
    if (record == NULL || out == NULL) {
        return WIRESHARK_I154_PARSE_ERR_ARG;
    }
    memset(out, 0, sizeof(*out));
    const uint8_t phy_len = record->frame[0];
    if (phy_len == 0u || (uint16_t)phy_len + 1u > NEARBY_I154_FRAME_STORAGE) {
        return WIRESHARK_I154_PARSE_ERR_MALFORMED;
    }
    out->meta.channel = record->frame_info.channel;
    out->meta.rssi = record->frame_info.rssi;
    out->meta.lqi = record->frame_info.lqi;
    out->meta.timestamp = record->frame_info.timestamp;
    out->meta.phy_length = phy_len;
    return wireshark_i154_parse_mac(record->frame + 1u, phy_len, &out->mac);
}

static void discard_ready_records(void)
{
    for (;;) {
        nearby_i154_rx_record_t *record = NULL;
        if (nearby_i154_rx_take(&record, 0) != ESP_OK) {
            break;
        }
        (void)nearby_i154_rx_release(record);
    }
}

static esp_err_t consume_batch(const kismet_i154_scan_config_t *config,
                               kismet_i154_scan_stats_t *stats,
                               bool *consumed_any)
{
    *consumed_any = false;
    for (unsigned n = 0; n < I154_BATCH_MAX; ++n) {
        nearby_i154_rx_record_t *record = NULL;
        if (nearby_i154_rx_take(&record, 0) != ESP_OK) {
            break;
        }
        *consumed_any = true;
        ++stats->raw_records;

        wireshark_i154_observation_t normalized;
        const wireshark_i154_parse_result_t mac_result =
            normalize_record(record, &normalized);
        wireshark_zigbee_discovery_facts_t zigbee;
        memset(&zigbee, 0, sizeof(zigbee));
        wireshark_zigbee_parse_result_t zigbee_result =
            WIRESHARK_ZIGBEE_PARSE_UNSUPPORTED;

        if (mac_result == WIRESHARK_I154_PARSE_ERR_ARG ||
            mac_result == WIRESHARK_I154_PARSE_ERR_MALFORMED) {
            ++stats->malformed_records;
        } else if (mac_result == WIRESHARK_I154_PARSE_SECURED) {
            ++stats->secured_records;
        } else if (mac_result == WIRESHARK_I154_PARSE_UNSUPPORTED) {
            ++stats->unsupported_records;
        } else {
            zigbee_result = wireshark_zigbee_parse(record->frame + 1u,
                                                   record->frame[0],
                                                   &normalized.mac,
                                                   &zigbee);
            if (zigbee_result == WIRESHARK_ZIGBEE_PARSE_SECURED) {
                ++stats->secured_records;
            } else if (zigbee_result == WIRESHARK_ZIGBEE_PARSE_ERR_ARG ||
                       zigbee_result == WIRESHARK_ZIGBEE_PARSE_ERR_MALFORMED) {
                ++stats->malformed_records;
            } else if (zigbee_result == WIRESHARK_ZIGBEE_PARSE_UNSUPPORTED) {
                ++stats->unsupported_records;
            } else if (zigbee.has_device_announce ||
                       zigbee.has_node_descriptor ||
                       zigbee.has_simple_descriptor ||
                       zigbee.active_endpoint_count > 0u ||
                       zigbee.has_manufacturer ||
                       zigbee.has_model) {
                ++stats->zigbee_discovery_records;
            }
        }

        esp_err_t callback_err = ESP_OK;
        if (config->observation_cb != NULL &&
            mac_result != WIRESHARK_I154_PARSE_ERR_ARG &&
            mac_result != WIRESHARK_I154_PARSE_ERR_MALFORMED) {
            callback_err = config->observation_cb(
                &normalized, mac_result, &zigbee, zigbee_result,
                config->observation_ctx);
            if (callback_err == ESP_OK) {
                ++stats->emitted_records;
            }
        }

        const esp_err_t release_err = nearby_i154_rx_release(record);
        if (release_err != ESP_OK) {
            return release_err;
        }
        if (callback_err != ESP_OK) {
            return callback_err;
        }
    }
    return ESP_OK;
}

static esp_err_t drain_ready(const kismet_i154_scan_config_t *config,
                             kismet_i154_scan_stats_t *stats)
{
    bool consumed = false;
    esp_err_t err = ESP_OK;
    do {
        consumed = false;
        err = consume_batch(config, stats, &consumed);
    } while (err == ESP_OK && consumed);
    return err;
}

esp_err_t kismet_i154_scan(const kismet_i154_scan_config_t *config,
                           kismet_i154_scan_stats_t *stats)
{
    if (config == NULL || stats == NULL || config->dwell_ms == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    if (scan_session_require_scan_owner() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t first = config->first_channel == 0u ? 11u : config->first_channel;
    uint8_t last = config->last_channel == 0u ? 26u : config->last_channel;
    if (first < 11u || last > 26u || first > last) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(stats, 0, sizeof(*stats));
    stats->drops_before = nearby_i154_rx_drop_count();

    TickType_t dwell_ticks = pdMS_TO_TICKS(config->dwell_ms);
    if (dwell_ticks == 0) dwell_ticks = 1;
    TickType_t poll_ticks = config->idle_poll_ticks;
    if (poll_ticks == 0) poll_ticks = 1;

    esp_err_t first_error = ESP_OK;
    for (uint8_t channel = first; channel <= last; ++channel) {
        esp_err_t err = nearby_i154_receive_start(channel);
        if (err != ESP_OK) {
            remember_error(err, &first_error);
            break;
        }
        const TickType_t start = xTaskGetTickCount();
        while ((TickType_t)(xTaskGetTickCount() - start) < dwell_ticks) {
            bool consumed = false;
            err = consume_batch(config, stats, &consumed);
            if (err != ESP_OK) {
                remember_error(err, &first_error);
                break;
            }
            if (!consumed) {
                const TickType_t elapsed =
                    (TickType_t)(xTaskGetTickCount() - start);
                const TickType_t remaining = elapsed < dwell_ticks
                                                 ? dwell_ticks - elapsed
                                                 : 0;
                if (remaining == 0) break;
                vTaskDelay(poll_ticks < remaining ? poll_ticks : remaining);
            }
        }

        remember_error(nearby_i154_receive_stop(), &first_error);
        if (first_error == ESP_OK) {
            remember_error(drain_ready(config, stats), &first_error);
        } else {
            discard_ready_records();
        }
        if (first_error != ESP_OK) {
            break;
        }
        ++stats->channels_completed;
    }

    if (nearby_i154_receive_is_active()) {
        remember_error(nearby_i154_receive_stop(), &first_error);
    }
    stats->drops_after = nearby_i154_rx_drop_count();
    return first_error;
}
