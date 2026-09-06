#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "nearby_transport_dedup.h"
#include "nearby_wifi_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_WIFI_ACTIVE_RECORD_MAX 24u

typedef esp_err_t (*nearby_wifi_active_cb_t)(
    const nearby_wifi_active_facts_t *facts,
    void *ctx);

typedef esp_err_t (*nearby_wifi_passive_cb_t)(
    const nearby_wifi_passive_normalized_t *observation,
    nearby_wifi_parse_result_t parse_result,
    void *ctx);

typedef struct {
    bool show_hidden;
    nearby_scan_dedup_t *dedup;
    nearby_wifi_active_cb_t observation_cb;
    void *observation_ctx;
} nearby_wifi_active_scan_config_t;

typedef struct {
    uint32_t records;
    uint32_t emitted_records;
    uint32_t duplicate_records;
} nearby_wifi_active_scan_stats_t;

typedef struct {
    uint8_t first_channel;
    uint8_t last_channel;
    uint32_t dwell_ms;
    TickType_t idle_poll_ticks;
    nearby_scan_dedup_t *dedup;
    nearby_wifi_passive_cb_t observation_cb;
    void *observation_ctx;
} nearby_wifi_passive_scan_config_t;

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
} nearby_wifi_passive_scan_stats_t;

esp_err_t nearby_wifi_active_scan_run(
    const nearby_wifi_active_scan_config_t *config,
    nearby_wifi_active_scan_stats_t *stats);

esp_err_t nearby_wifi_passive_scan_run(
    const nearby_wifi_passive_scan_config_t *config,
    nearby_wifi_passive_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
