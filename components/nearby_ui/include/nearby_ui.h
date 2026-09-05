#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_UI_SCAN_IDLE = 0,
    NEARBY_UI_SCAN_SCANNING,
} nearby_ui_scan_state_t;

typedef struct {
    const char *device_id;
    const char *name;
    const char *icon;
    const char *manufacturer;
    const char *model;
} nearby_ui_device_t;

typedef struct {
    void (*scan_requested)(void *ctx);
    void (*portal_start_requested)(void *ctx);
    void (*portal_stop_requested)(void *ctx);
    void *ctx;
} nearby_ui_callbacks_t;

/*
 * Create the 170x320 UI on the active LVGL screen.
 * Boot is always IDLE; this function never starts a scan.
 */
void nearby_ui_init(const nearby_ui_callbacks_t *callbacks);

/* Scanner-agnostic session events. Progress advances only on module completion. */
void nearby_ui_scan_begin(uint16_t total_modules);
void nearby_ui_scan_module_complete(void);
void nearby_ui_scan_end(void);
nearby_ui_scan_state_t nearby_ui_scan_state(void);

/* Insert/update visible HA-style Device data as discoveries arrive. */
void nearby_ui_device_upsert(const nearby_ui_device_t *device);
void nearby_ui_clear_devices(void);

/* Device-local status rendered by Settings/Web Management screens. */
void nearby_ui_set_versions(const char *database_status, const char *firmware_version);
void nearby_ui_set_portal_info(bool running,
                               const char *ssid,
                               const char *address,
                               const char *pin);

/*
 * Mock/demo helpers for Agent E bring-up. The mock timer emits synthetic
 * module-completion events; production integrations should call the scan event
 * functions above from Agent B/D completion notifications instead.
 */
void nearby_ui_mock_seed_devices(void);
void nearby_ui_mock_start_scan(void);

#ifdef __cplusplus
}
#endif
