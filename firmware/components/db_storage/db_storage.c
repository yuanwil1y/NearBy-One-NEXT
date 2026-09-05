#include "db_storage.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nearby_board.h"
#include "scan_session.h"

static FILE *s_part_file;
static bool s_prepared;
static size_t s_expected_bytes;
static size_t s_written_bytes;
static nearby_db_validation_hooks_t s_hooks;

static esp_err_t require_storage_lease(void)
{
    return scan_session_require_competing_active();
}

static esp_err_t ensure_db_directories(void)
{
    if (mkdir(NEARBY_BOARD_SD_MOUNT_POINT "/nearby", 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    if (mkdir(NEARBY_BOARD_SD_MOUNT_POINT "/nearby/db", 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t nearby_db_storage_prepare_whole_sd(bool confirmed)
{
    if (require_storage_lease() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!confirmed || s_part_file != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_prepared = false;
    esp_err_t err = nearby_board_sd_format_whole(true);
    if (err != ESP_OK) {
        return err;
    }
    err = ensure_db_directories();
    if (err == ESP_OK) {
        s_prepared = true;
    }
    return err;
}

esp_err_t nearby_db_upload_begin(size_t expected_bytes,
                                 const nearby_db_validation_hooks_t *hooks)
{
    if (require_storage_lease() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_prepared || s_part_file != NULL || hooks == NULL || hooks->on_finish == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)unlink(NEARBY_DB_PART_PATH);
    s_part_file = fopen(NEARBY_DB_PART_PATH, "wb");
    if (s_part_file == NULL) {
        return ESP_FAIL;
    }

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
    if (require_storage_lease() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_part_file == NULL || (len > 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return ESP_OK;
    }
    if (s_expected_bytes != 0 && len > s_expected_bytes - s_written_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t written = fwrite(data, 1, len, s_part_file);
    if (written != len) {
        return ESP_FAIL;
    }
    s_written_bytes += written;

    if (s_hooks.on_chunk != NULL) {
        esp_err_t err = s_hooks.on_chunk(data, len, s_hooks.ctx);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t nearby_db_upload_finish(void)
{
    if (require_storage_lease() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_part_file == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
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

    esp_err_t verify_err = s_hooks.on_finish(NEARBY_DB_PART_PATH,
                                             s_written_bytes,
                                             s_hooks.ctx);
    if (verify_err != ESP_OK) {
        if (s_hooks.on_cancel != NULL) {
            s_hooks.on_cancel(s_hooks.ctx);
        }
        (void)unlink(NEARBY_DB_PART_PATH);
        return verify_err;
    }

    (void)unlink(NEARBY_DB_FINAL_PATH);
    if (rename(NEARBY_DB_PART_PATH, NEARBY_DB_FINAL_PATH) != 0) {
        (void)unlink(NEARBY_DB_PART_PATH);
        return ESP_FAIL;
    }

    s_prepared = false;
    memset(&s_hooks, 0, sizeof(s_hooks));
    s_expected_bytes = 0;
    return ESP_OK;
}

esp_err_t nearby_db_upload_cancel(void)
{
    if (s_part_file != NULL) {
        (void)fclose(s_part_file);
        s_part_file = NULL;
    }
    (void)unlink(NEARBY_DB_PART_PATH);
    if (s_hooks.on_cancel != NULL) {
        s_hooks.on_cancel(s_hooks.ctx);
    }
    memset(&s_hooks, 0, sizeof(s_hooks));
    s_expected_bytes = 0;
    s_written_bytes = 0;
    return ESP_OK;
}

bool nearby_db_upload_is_active(void)
{
    return s_part_file != NULL;
}

size_t nearby_db_upload_bytes_written(void)
{
    return s_written_bytes;
}
