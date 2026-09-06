#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "nearby_ble_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*nearby_ble_observation_cb_t)(
    const nearby_ble_normalized_t *observation,
    nearby_ble_parse_result_t parse_result,
    void *ctx);

typedef struct {
    uint32_t duration_ms;
    bool passive;
    TickType_t sync_timeout_ticks;
    TickType_t idle_poll_ticks;
    nearby_ble_observation_cb_t observation_cb;
    void *observation_ctx;
} nearby_ble_scan_config_t;

typedef struct {
    uint32_t legacy_records;
    uint32_t extended_records;
    uint32_t emitted_records;
    uint32_t partial_records;
    uint32_t malformed_records;
    uint32_t legacy_drops_before;
    uint32_t legacy_drops_after;
    uint32_t extended_drops_before;
    uint32_t extended_drops_after;
} nearby_ble_scan_stats_t;

/*
 * Run one finite NimBLE discovery phase while the caller owns D's complete-scan
 * lease. Observations are callback-scoped normalized facts for C; callers must
 * copy anything they retain. The function always attempts stop/deinit before
 * returning and never acquires/releases the product scan gate itself.
 */
esp_err_t nearby_ble_scan_run(const nearby_ble_scan_config_t *config,
                              nearby_ble_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
