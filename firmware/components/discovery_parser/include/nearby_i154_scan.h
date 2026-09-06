#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "nearby_i154_discovery.h"
#include "nearby_zigbee_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*nearby_i154_observation_cb_t)(
    const nearby_i154_normalized_t *observation,
    nearby_i154_parse_result_t mac_result,
    const nearby_zigbee_discovery_facts_t *zigbee,
    nearby_zigbee_parse_result_t zigbee_result,
    void *ctx);

typedef struct {
    uint8_t first_channel;
    uint8_t last_channel;
    uint32_t dwell_ms;
    TickType_t idle_poll_ticks;
    nearby_i154_observation_cb_t observation_cb;
    void *observation_ctx;
} nearby_i154_scan_config_t;

typedef struct {
    uint32_t raw_records;
    uint32_t emitted_records;
    uint32_t malformed_records;
    uint32_t secured_records;
    uint32_t unsupported_records;
    uint32_t zigbee_discovery_records;
    uint32_t drops_before;
    uint32_t drops_after;
    uint8_t channels_completed;
} nearby_i154_scan_stats_t;

esp_err_t nearby_i154_scan_run(const nearby_i154_scan_config_t *config,
                               nearby_i154_scan_stats_t *stats);

#ifdef __cplusplus
}
#endif
