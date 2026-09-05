#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize the product-level exclusive scan gate. Safe to call repeatedly. */
esp_err_t scan_session_init(void);

/**
 * Acquire exclusive ownership for one complete discovery scan session.
 *
 * The coordinator takes this once before the first scanner module and releases
 * it only after the last scanner module. Wi-Fi/BLE/802.15.4 phase switches do
 * not release the gate between modules.
 */
esp_err_t scan_session_begin(TickType_t timeout_ticks);

/** End the scan session. Must be called by the task that began it. */
esp_err_t scan_session_end(void);

/** True while a complete scan session owns the gate. */
bool scan_session_is_active(void);

/** Return ESP_OK only to the task that currently owns the complete scan. */
esp_err_t scan_session_require_scan_owner(void);

/** Monotonic generation incremented each time a scan session starts. */
uint32_t scan_session_generation(void);

/**
 * Try to reserve the same gate for a user/device/standalone RF operation.
 *
 * This is intentionally non-blocking. While a scan session is active the
 * caller gets ESP_ERR_INVALID_STATE and should surface BUSY to UI/Web code.
 * Never call from an ISR or Wi-Fi/NimBLE driver callback.
 */
esp_err_t scan_session_competing_op_try_begin(void);

/** Return ESP_OK only to the task that owns the current competing operation. */
esp_err_t scan_session_require_competing_owner(void);

/** Release a successful competing-op reservation from the same task. */
esp_err_t scan_session_competing_op_end(void);

/**
 * Error-path helper for the current owner task.
 *
 * If the calling task owns either a complete scan or a competing reservation,
 * release it. If it owns neither, return ESP_ERR_INVALID_STATE. This function
 * deliberately does not force-release a mutex owned by another task.
 */
esp_err_t scan_session_cleanup_owned(void);

#ifdef __cplusplus
}
#endif
