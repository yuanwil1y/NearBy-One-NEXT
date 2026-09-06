#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NEARBY_UI_SCAN_IDLE = 0,
    NEARBY_UI_SCAN_SCANNING,
} nearby_ui_scan_state_t;

typedef struct {
    void (*scan_requested)(void *ctx);
    void (*portal_start_requested)(void *ctx);
    void (*portal_stop_requested)(void *ctx);
    void *ctx;
} nearby_ui_callbacks_t;

/* Create the 170x320 UI on the active LVGL display. Boot is idle and empty. */
void nearby_ui_init(const nearby_ui_callbacks_t *callbacks);

/* Scanner progress is driven only by real B coordinator terminal events. */
void nearby_ui_scan_begin(uint16_t total_modules);
void nearby_ui_scan_module_complete(void);
void nearby_ui_scan_end(void);
nearby_ui_scan_state_t nearby_ui_scan_state(void);

/* Rebuild visible Device/Entity/State views from Agent A's owner-task registry. */
void nearby_ui_refresh_from_ha(void);

void nearby_ui_set_versions(const char *database_status, const char *firmware_version);
void nearby_ui_set_portal_info(bool running,
                               const char *ssid,
                               const char *address,
                               const char *pin);

#ifdef __cplusplus
}
#endif
