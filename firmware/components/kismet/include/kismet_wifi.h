#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "kismet_types.h"
#include "wireshark_80211.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KISMET_WIFI_ACTIVE_RECORD_MAX 24u

typedef esp_err_t (*kismet_wifi_active_cb_t)(
    const kismet_wifi_active_facts_t *facts,
    void *ctx);

typedef esp_err_t (*kismet_wifi_passive_cb_t)(
    const wireshark_80211_passive_observation_t *observation,
    wireshark_80211_parse_result_t parse_result,
    void *ctx);

typedef struct {
    bool show_hidden;
    kismet_dedup_t *dedup;
    kismet_wifi_active_cb_t observation_cb;
    void *observation_ctx;
} kismet_wifi_active_scan_config_t;

typedef struct {
    uint32_t records;
    uint32_t emitted_records;
    uint32_t duplicate_records;
} kismet_wifi_active_scan_stats_t;

typedef struct {
    uint8_t first_channel;
    uint8_t last_channel;
    uint32_t dwell_ms;
    TickType_t idle_poll_ticks;
    kismet_dedup_t *dedup;
    kismet_wifi_passive_cb_t observation_cb;
    void *observation_ctx;
} kismet_wifi_passive_scan_config_t;

typedef struct {
    uint32_t raw_records;
    uint32_t emitted_records;
    uint32_t duplicate_records;
    uint32_t partial_records;
    uint32_t malformed_records;
    uint32_t unsupported_records;
    uint32_t drops_before;
    uint32_t drops_after;
    uint8_t channels_completed;
} kismet_wifi_passive_scan_stats_t;

/* Finite Wi-Fi acquisition primitives. Product sequencing remains external. */
esp_err_t kismet_wifi_scan_active(
    const kismet_wifi_active_scan_config_t *config,
    kismet_wifi_active_scan_stats_t *stats);

esp_err_t kismet_wifi_scan_passive(
    const kismet_wifi_passive_scan_config_t *config,
    kismet_wifi_passive_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
