#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "nearby_semantic_apply.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_PIPELINE_EVENT_MATCHED = 0,
    NEARBY_PIPELINE_EVENT_MODULE_TERMINAL,
    NEARBY_PIPELINE_EVENT_SCAN_DONE,
    NEARBY_PIPELINE_EVENT_FATAL,
} nearby_pipeline_event_type_t;

typedef struct {
    nearby_pipeline_event_type_t type;
    esp_err_t error;
    uint8_t terminal_count;
    nearby_matched_semantic_t matched;
} nearby_pipeline_event_t;

/* Initializes one fixed scan worker and bounded worker->owner event queue. */
esp_err_t nearby_scan_pipeline_init(void);

/* Queue one complete fixed seven-module pass. Owner task calls this from UI. */
esp_err_t nearby_scan_pipeline_request(void);

/* Owner-task receive. Only MATCHED events are legal inputs to Agent A. */
bool nearby_scan_pipeline_receive(nearby_pipeline_event_t *event, TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
