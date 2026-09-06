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

typedef enum {
    NEARBY_DB_FORMAT_IDLE = 0,
    NEARBY_DB_FORMAT_RUNNING,
    NEARBY_DB_FORMAT_READY,
    NEARBY_DB_FORMAT_FAILED,
} nearby_db_format_state_t;

esp_err_t nearby_db_storage_set_preflight_safe_to_format(bool safe_to_format);
bool nearby_db_storage_preflight_is_authorized(void);

/** Start destructive whole-card preparation on a dedicated worker task. */
esp_err_t nearby_db_storage_prepare_whole_sd(bool confirmed);
nearby_db_format_state_t nearby_db_storage_format_state(void);
const char *nearby_db_storage_format_state_name(nearby_db_format_state_t state);
const char *nearby_db_storage_format_stage(void);
esp_err_t nearby_db_storage_format_error(void);
bool nearby_db_storage_format_is_active(void);

esp_err_t nearby_db_upload_begin(size_t expected_bytes,
                                 const nearby_db_validation_hooks_t *hooks);
esp_err_t nearby_db_upload_write(const uint8_t *data, size_t len);
esp_err_t nearby_db_upload_finish(void);
esp_err_t nearby_db_upload_cancel(void);
bool nearby_db_upload_is_active(void);
size_t nearby_db_upload_bytes_written(void);

#ifdef __cplusplus
}
#endif
