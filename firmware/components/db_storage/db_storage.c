#include "db_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nearby_board.h"
#include "scan_session.h"

#define FORMAT_TASK_STACK 4096U
#define FORMAT_TASK_PRIORITY 3U
#define FORMAT_TIMEOUT_US (60LL * 1000LL * 1000LL)

static const char *TAG = "db-storage";
static FILE *s_part_file;
static bool s_preflight_safe_to_format;
static bool s_prepared;
static size_t s_expected_bytes;
static size_t s_written_bytes;
static nearby_db_validation_hooks_t s_hooks;
static volatile nearby_db_format_state_t s_format_state = NEARBY_DB_FORMAT_IDLE;
static volatile esp_err_t s_format_error = ESP_OK;
static volatile TaskHandle_t s_format_task;
static esp_timer_handle_t s_format_timeout_timer;

static esp_err_t require_storage_lease(void)
{
    return scan_session_require_competing_active();
}

static esp_err_t ensure_db_directories(void)
{
    if (mkdir(NEARBY_BOARD_SD_MOUNT_POINT "/nearby", 0775) != 0 && errno != EEXIST) return ESP_FAIL;
    if (mkdir(NEARBY_BOARD_SD_MOUNT_POINT "/nearby/db", 0775) != 0 && errno != EEXIST) return ESP_FAIL;
    return ESP_OK;
}

const char *nearby_db_storage_format_state_name(nearby_db_format_state_t state)
{
    switch (state) {
    case NEARBY_DB_FORMAT_RUNNING: return "running";
    case NEARBY_DB_FORMAT_READY: return "ready";
    case NEARBY_DB_FORMAT_FAILED: return "failed";
    case NEARBY_DB_FORMAT_IDLE:
    default: return "idle";
    }
}

nearby_db_format_state_t nearby_db_storage_format_state(void) { return s_format_state; }
esp_err_t nearby_db_storage_format_error(void) { return s_format_error; }
bool nearby_db_storage_format_is_active(void) { return s_format_task != NULL; }
const char *nearby_db_storage_format_stage(void)
{
    return nearby_board_sd_format_stage_name(nearby_board_sd_format_stage());
}

static void format_timeout_cb(void *arg)
{
    (void)arg;
    if (s_format_task != NULL && s_format_state == NEARBY_DB_FORMAT_RUNNING) {
        s_format_error = ESP_ERR_TIMEOUT;
        s_format_state = NEARBY_DB_FORMAT_FAILED;
        s_prepared = false;
        ESP_LOGE(TAG, "whole-SD format deadline exceeded stage=%s", nearby_db_storage_format_stage());
    }
}

static esp_err_t ensure_timeout_timer(void)
{
    if (s_format_timeout_timer != NULL) return ESP_OK;
    const esp_timer_create_args_t args = {
        .callback = format_timeout_cb,
        .name = "dbfmt-timeout",
    };
    return esp_timer_create(&args, &s_format_timeout_timer);
}

static void format_worker(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "whole-SD format worker started");
    esp_err_t err = nearby_board_sd_format_whole(true);
    if (err == ESP_OK) err = ensure_db_directories();
    if (s_format_timeout_timer != NULL) (void)esp_timer_stop(s_format_timeout_timer);

    if (s_format_state == NEARBY_DB_FORMAT_RUNNING) {
        s_format_error = err;
        s_prepared = err == ESP_OK;
        s_format_state = err == ESP_OK ? NEARBY_DB_FORMAT_READY : NEARBY_DB_FORMAT_FAILED;
    } else {
        s_prepared = false;
        ESP_LOGW(TAG, "whole-SD format returned after deadline; result discarded: %s", esp_err_to_name(err));
    }
    if (err == ESP_OK) ESP_LOGI(TAG, "whole-SD format prepared for upload");
    else ESP_LOGE(TAG, "whole-SD format worker failed: %s", esp_err_to_name(err));
    s_format_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t nearby_db_storage_set_preflight_safe_to_format(bool safe_to_format)
{
    if (require_storage_lease() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_part_file != NULL || s_format_task != NULL) return ESP_ERR_INVALID_STATE;
    if (!safe_to_format) {
        s_preflight_safe_to_format = false;
        s_prepared = false;
        s_format_state = NEARBY_DB_FORMAT_IDLE;
        s_format_error = ESP_OK;
        return ESP_OK;
    }
    if (s_prepared) return ESP_ERR_INVALID_STATE;
    s_preflight_safe_to_format = true;
    return ESP_OK;
}

bool nearby_db_storage_preflight_is_authorized(void) { return s_preflight_safe_to_format; }

esp_err_t nearby_db_storage_prepare_whole_sd(bool confirmed)
{
    if (require_storage_lease() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (!confirmed || !s_preflight_safe_to_format || s_part_file != NULL ||
        s_prepared || s_format_task != NULL) return ESP_ERR_INVALID_STATE;

    s_preflight_safe_to_format = false;
    s_prepared = false;
    s_format_error = ESP_OK;
    s_format_state = NEARBY_DB_FORMAT_RUNNING;

    esp_err_t err = ensure_timeout_timer();
    if (err != ESP_OK) goto fail;
    err = esp_timer_start_once(s_format_timeout_timer, FORMAT_TIMEOUT_US);
    if (err != ESP_OK) goto fail;

    TaskHandle_t task = NULL;
    s_format_task = (TaskHandle_t)1;
    if (xTaskCreate(format_worker, "db_format", FORMAT_TASK_STACK, NULL,
                    FORMAT_TASK_PRIORITY, &task) != pdPASS) {
        s_format_task = NULL;
        (void)esp_timer_stop(s_format_timeout_timer);
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    if (s_format_task != NULL) s_format_task = task;
    return ESP_OK;

fail:
    s_format_error = err;
    s_format_state = NEARBY_DB_FORMAT_FAILED;
    return err;
}

esp_err_t nearby_db_upload_begin(size_t expected_bytes,
                                 const nearby_db_validation_hooks_t *hooks)
{
    if (require_storage_lease() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (!s_prepared || s_format_state != NEARBY_DB_FORMAT_READY || s_part_file != NULL ||
        hooks == NULL || hooks->on_finish == NULL) return ESP_ERR_INVALID_STATE;

    (void)unlink(NEARBY_DB_PART_PATH);
    s_part_file = fopen(NEARBY_DB_PART_PATH, "wb");
    if (s_part_file == NULL) return ESP_FAIL;
    s_expected_bytes = expected_bytes;
    s_written_bytes = 0;
    s_hooks = *hooks;
    if (s_hooks.on_begin != NULL) {
        esp_err_t err = s_hooks.on_begin(expected_bytes, s_hooks.ctx);
        if (err != ESP_OK) {
            (void)nearby_db_upload_cancel();
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t nearby_db_upload_write(const uint8_t *data, size_t len)
{
    if (require_storage_lease() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_part_file == NULL || (len > 0 && data == NULL)) return ESP_ERR_INVALID_ARG;
    if (len == 0) return ESP_OK;
    if (s_expected_bytes != 0 &&
        (s_written_bytes > s_expected_bytes || len > s_expected_bytes - s_written_bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t written = fwrite(data, 1, len, s_part_file);
    if (written != len) return ESP_FAIL;
    s_written_bytes += written;
    if (s_hooks.on_chunk != NULL) {
        esp_err_t err = s_hooks.on_chunk(data, len, s_hooks.ctx);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t nearby_db_upload_finish(void)
{
    if (require_storage_lease() != ESP_OK) return ESP_ERR_INVALID_STATE;
    if (s_part_file == NULL) return ESP_ERR_INVALID_STATE;
    if (s_expected_bytes != 0 && s_written_bytes != s_expected_bytes) {
        (void)nearby_db_upload_cancel();
        return ESP_ERR_INVALID_SIZE;
    }
    if (fflush(s_part_file) != 0 || fsync(fileno(s_part_file)) != 0) {
        (void)nearby_db_upload_cancel();
        return ESP_FAIL;
    }
    if (fclose(s_part_file) != 0) {
        s_part_file = NULL;
        (void)unlink(NEARBY_DB_PART_PATH);
        return ESP_FAIL;
    }
    s_part_file = NULL;

    esp_err_t verify_err = s_hooks.on_finish(NEARBY_DB_PART_PATH, s_written_bytes, s_hooks.ctx);
    if (verify_err != ESP_OK) {
        if (s_hooks.on_cancel != NULL) s_hooks.on_cancel(s_hooks.ctx);
        (void)unlink(NEARBY_DB_PART_PATH);
        return verify_err;
    }
    (void)unlink(NEARBY_DB_FINAL_PATH);
    if (rename(NEARBY_DB_PART_PATH, NEARBY_DB_FINAL_PATH) != 0) {
        (void)unlink(NEARBY_DB_PART_PATH);
        return ESP_FAIL;
    }

    s_prepared = false;
    s_format_state = NEARBY_DB_FORMAT_IDLE;
    memset(&s_hooks, 0, sizeof(s_hooks));
    s_expected_bytes = 0;
    s_written_bytes = 0;
    return ESP_OK;
}

esp_err_t nearby_db_upload_cancel(void)
{
    if (s_part_file != NULL) {
        (void)fclose(s_part_file);
        s_part_file = NULL;
    }
    (void)unlink(NEARBY_DB_PART_PATH);
    if (s_hooks.on_cancel != NULL) s_hooks.on_cancel(s_hooks.ctx);
    memset(&s_hooks, 0, sizeof(s_hooks));
    s_expected_bytes = 0;
    s_written_bytes = 0;
    return ESP_OK;
}

bool nearby_db_upload_is_active(void) { return s_part_file != NULL; }
size_t nearby_db_upload_bytes_written(void) { return s_written_bytes; }
