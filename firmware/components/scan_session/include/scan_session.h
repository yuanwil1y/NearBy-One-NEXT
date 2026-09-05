#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t scan_session_init(void);
esp_err_t scan_session_begin(TickType_t timeout_ticks);
esp_err_t scan_session_end(void);
bool scan_session_is_active(void);
esp_err_t scan_session_require_scan_owner(void);
uint32_t scan_session_generation(void);

esp_err_t scan_session_competing_op_try_begin(void);
/** True while a competing operation lease owns the product gate. */
bool scan_session_competing_op_is_active(void);
/**
 * Return ESP_OK to any task executing inside an already-active competing
 * operation lifecycle (for example esp_http_server's worker task). This does
 * not grant permission to release the gate.
 */
esp_err_t scan_session_require_competing_active(void);
/** Return ESP_OK only to the task that owns/releases the competing lease. */
esp_err_t scan_session_require_competing_owner(void);
esp_err_t scan_session_competing_op_end(void);

/** Error-path helper that only releases a lease owned by the current task. */
esp_err_t scan_session_cleanup_owned(void);

#ifdef __cplusplus
}
#endif
