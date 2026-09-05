#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEARBY_DB_PART_PATH  "/sdcard/nearby/db/nearby.nbdb.part"
#define NEARBY_DB_FINAL_PATH "/sdcard/nearby/db/nearby.nbdb"

typedef struct {
    esp_err_t (*on_begin)(size_t expected_bytes, void *ctx);
    esp_err_t (*on_chunk)(const uint8_t *data, size_t len, void *ctx);
    esp_err_t (*on_finish)(const char *part_path, size_t total_bytes, void *ctx);
    void (*on_cancel)(void *ctx);
    void *ctx;
} nearby_db_validation_hooks_t;

/** Explicit destructive step. Requires the Web/upper-layer competing-op owner. */
esp_err_t nearby_db_storage_prepare_whole_sd(bool confirmed);

/** Start a bounded direct-to-SD upload after prepare_whole_sd succeeds. */
esp_err_t nearby_db_upload_begin(size_t expected_bytes,
                                 const nearby_db_validation_hooks_t *hooks);

/** Write exactly this chunk to `.part`; no whole-file buffering is performed. */
esp_err_t nearby_db_upload_write(const uint8_t *data, size_t len);

/**
 * Flush, verify exact byte count (when nonzero), invoke C-owned finalize hook,
 * and only then promote `.part` to the final DB path.
 */
esp_err_t nearby_db_upload_finish(void);

/** Close and delete `.part`; safe to call after interrupted HTTP transfers. */
esp_err_t nearby_db_upload_cancel(void);

bool nearby_db_upload_is_active(void);
size_t nearby_db_upload_bytes_written(void);

#ifdef __cplusplus
}
#endif
