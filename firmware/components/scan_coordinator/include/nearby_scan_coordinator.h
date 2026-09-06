#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_SCAN_MODULE_WIFI_ACTIVE = 0,
    NEARBY_SCAN_MODULE_WIFI_MANAGEMENT,
    NEARBY_SCAN_MODULE_ZEROCONF,
    NEARBY_SCAN_MODULE_SSDP,
    NEARBY_SCAN_MODULE_DHCP,
    NEARBY_SCAN_MODULE_BLE,
    NEARBY_SCAN_MODULE_IEEE802154,
    NEARBY_SCAN_MODULE_COUNT,
} nearby_scan_module_t;

#define NEARBY_SCAN_MODULE_BIT(module) (1u << (uint32_t)(module))
#define NEARBY_SCAN_ALL_MODULES_MASK ((1u << NEARBY_SCAN_MODULE_COUNT) - 1u)

typedef enum {
    NEARBY_SCAN_MODULE_DISABLED = 0,
    NEARBY_SCAN_MODULE_PENDING,
    NEARBY_SCAN_MODULE_RUNNING,
    NEARBY_SCAN_MODULE_COMPLETE,
    NEARBY_SCAN_MODULE_SKIPPED,
    NEARBY_SCAN_MODULE_ERROR,
    NEARBY_SCAN_MODULE_CANCELLED,
} nearby_scan_module_status_t;

typedef enum {
    NEARBY_SCAN_STATE_IDLE = 0,
    NEARBY_SCAN_STATE_STARTING,
    NEARBY_SCAN_STATE_RUNNING,
    NEARBY_SCAN_STATE_FINISHING,
    NEARBY_SCAN_STATE_COMPLETE,
    NEARBY_SCAN_STATE_FATAL,
} nearby_scan_state_t;

typedef struct {
    nearby_scan_module_status_t status;
    esp_err_t error;
} nearby_scan_module_outcome_t;

typedef struct {
    nearby_scan_state_t state;
    uint32_t generation;
    uint32_t enabled_mask;
    nearby_scan_module_status_t module_status[NEARBY_SCAN_MODULE_COUNT];
    uint8_t enabled_count;
    uint8_t terminal_count;
    uint8_t progress_percent;
    esp_err_t error;
} nearby_scan_event_t;

typedef void (*nearby_scan_event_cb_t)(const nearby_scan_event_t *event,
                                       void *ctx);
typedef nearby_scan_module_outcome_t (*nearby_scan_module_runner_t)(void *ctx);

/* Fixed scanner slots: this is intentionally not a generic provider/plugin registry. */
typedef struct {
    nearby_scan_module_runner_t wifi_active;
    nearby_scan_module_runner_t wifi_management;
    nearby_scan_module_runner_t zeroconf;
    nearby_scan_module_runner_t ssdp;
    nearby_scan_module_runner_t dhcp;
    nearby_scan_module_runner_t ble;
    nearby_scan_module_runner_t ieee802154;
    void *ctx;
} nearby_scan_runners_t;

typedef struct {
    nearby_scan_state_t state;
    uint32_t generation;
    uint32_t enabled_mask;
    nearby_scan_module_status_t module_status[NEARBY_SCAN_MODULE_COUNT];
    uint8_t enabled_count;
    uint8_t terminal_count;
    esp_err_t first_module_error;
    bool gate_held;
    nearby_scan_event_cb_t event_cb;
    void *event_ctx;
} nearby_scan_coordinator_t;

void nearby_scan_coordinator_init(nearby_scan_coordinator_t *coordinator,
                                  nearby_scan_event_cb_t event_cb,
                                  void *event_ctx);

esp_err_t nearby_scan_coordinator_begin(nearby_scan_coordinator_t *coordinator,
                                        uint32_t enabled_mask,
                                        TickType_t gate_timeout_ticks);

esp_err_t nearby_scan_coordinator_module_start(
    nearby_scan_coordinator_t *coordinator,
    nearby_scan_module_t module);

esp_err_t nearby_scan_coordinator_module_terminal(
    nearby_scan_coordinator_t *coordinator,
    nearby_scan_module_t module,
    nearby_scan_module_status_t terminal_status,
    esp_err_t error);

esp_err_t nearby_scan_coordinator_finish(nearby_scan_coordinator_t *coordinator);
esp_err_t nearby_scan_coordinator_cancel(nearby_scan_coordinator_t *coordinator,
                                         esp_err_t reason);

/* Retry D cleanup after a fatal teardown. The product gate remains held until this succeeds. */
esp_err_t nearby_scan_coordinator_recover(nearby_scan_coordinator_t *coordinator);

/* Run all enabled fixed slots in hardware-safe serial order under one D scan lease. */
esp_err_t nearby_scan_coordinator_run(nearby_scan_coordinator_t *coordinator,
                                      uint32_t enabled_mask,
                                      TickType_t gate_timeout_ticks,
                                      const nearby_scan_runners_t *runners);

uint8_t nearby_scan_coordinator_progress(const nearby_scan_coordinator_t *coordinator);
bool nearby_scan_coordinator_gate_held(const nearby_scan_coordinator_t *coordinator);
esp_err_t nearby_scan_coordinator_first_module_error(
    const nearby_scan_coordinator_t *coordinator);

#ifdef __cplusplus
}
#endif
