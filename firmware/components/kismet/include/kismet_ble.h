#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "kismet_types.h"
#include "wireshark_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*kismet_ble_observation_cb_t)(
    const wireshark_ble_observation_t *observation,
    wireshark_ble_parse_result_t parse_result,
    void *ctx);

typedef struct {
    uint32_t duration_ms;
    bool passive;
    TickType_t sync_timeout_ticks;
    TickType_t idle_poll_ticks;
    kismet_dedup_t *dedup;
    kismet_ble_observation_cb_t observation_cb;
    void *observation_ctx;
} kismet_ble_scan_config_t;

typedef struct {
    uint32_t legacy_records;
    uint32_t extended_records;
    uint32_t emitted_records;
    uint32_t duplicate_records;
    uint32_t partial_records;
    uint32_t malformed_records;
    uint32_t legacy_drops_before;
    uint32_t legacy_drops_after;
    uint32_t extended_drops_before;
    uint32_t extended_drops_after;
} kismet_ble_scan_stats_t;

/* Finite BLE acquisition primitive. Product sequencing remains external. */
esp_err_t kismet_ble_scan(const kismet_ble_scan_config_t *config,
                          kismet_ble_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
