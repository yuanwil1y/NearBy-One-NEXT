from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"{path}: expected one match, got {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


board_h = "firmware/components/nearby_board/include/nearby_board.h"
replace_once(board_h,
'''/** Mount the card as FATFS on the already initialized shared SPI2 bus. */
esp_err_t nearby_board_sd_mount(bool format_if_mount_failed);''',
'''typedef enum {
    NEARBY_BOARD_SD_FORMAT_IDLE = 0,
    NEARBY_BOARD_SD_FORMAT_UNMOUNT,
    NEARBY_BOARD_SD_FORMAT_RAW_OPEN,
    NEARBY_BOARD_SD_FORMAT_ERASE_HEAD,
    NEARBY_BOARD_SD_FORMAT_ERASE_TAIL,
    NEARBY_BOARD_SD_FORMAT_RAW_CLOSE,
    NEARBY_BOARD_SD_FORMAT_REMOUNT_FAT,
    NEARBY_BOARD_SD_FORMAT_COMPLETE,
} nearby_board_sd_format_stage_t;

/** Mount the card as FATFS on the already initialized shared SPI2 bus. */
esp_err_t nearby_board_sd_mount(bool format_if_mount_failed);

nearby_board_sd_format_stage_t nearby_board_sd_format_stage(void);
const char *nearby_board_sd_format_stage_name(nearby_board_sd_format_stage_t stage);
esp_err_t nearby_board_sd_format_error(void);''')

Path("firmware/components/nearby_board/nearby_storage.c").write_text(r'''#include "nearby_board.h"

#include <string.h>

#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#define SD_MAX_FILES             6
#define SD_ALLOC_UNIT_BYTES      4096
#define PARTITION_METADATA_HEAD  34U
#define PARTITION_METADATA_TAIL  33U

static const char *TAG = "nearby-sd";
static sdmmc_card_t *s_card;
static volatile nearby_board_sd_format_stage_t s_format_stage = NEARBY_BOARD_SD_FORMAT_IDLE;
static volatile esp_err_t s_format_error = ESP_OK;

const char *nearby_board_sd_format_stage_name(nearby_board_sd_format_stage_t stage)
{
    switch (stage) {
    case NEARBY_BOARD_SD_FORMAT_UNMOUNT: return "unmount";
    case NEARBY_BOARD_SD_FORMAT_RAW_OPEN: return "raw-open";
    case NEARBY_BOARD_SD_FORMAT_ERASE_HEAD: return "erase-head";
    case NEARBY_BOARD_SD_FORMAT_ERASE_TAIL: return "erase-tail";
    case NEARBY_BOARD_SD_FORMAT_RAW_CLOSE: return "raw-close";
    case NEARBY_BOARD_SD_FORMAT_REMOUNT_FAT: return "remount-fat";
    case NEARBY_BOARD_SD_FORMAT_COMPLETE: return "complete";
    case NEARBY_BOARD_SD_FORMAT_IDLE:
    default: return "idle";
    }
}

nearby_board_sd_format_stage_t nearby_board_sd_format_stage(void)
{
    return s_format_stage;
}

esp_err_t nearby_board_sd_format_error(void)
{
    return s_format_error;
}

static void format_stage(nearby_board_sd_format_stage_t stage)
{
    s_format_stage = stage;
    s_format_error = ESP_OK;
    ESP_LOGI(TAG, "whole-SD format stage=%s", nearby_board_sd_format_stage_name(stage));
}

static esp_err_t format_fail(esp_err_t err)
{
    s_format_error = err;
    ESP_LOGE(TAG, "whole-SD format failed stage=%s err=%s (0x%x)",
             nearby_board_sd_format_stage_name(s_format_stage), esp_err_to_name(err), err);
    return err;
}

static sdspi_device_config_t sd_device_config(void)
{
    sdspi_device_config_t config = SDSPI_DEVICE_CONFIG_DEFAULT();
    config.host_id = NEARBY_BOARD_SPI_HOST;
    config.gpio_cs = NEARBY_BOARD_SD_CS_GPIO;
    return config;
}

esp_err_t nearby_board_sd_mount(bool format_if_mount_failed)
{
    if (s_card != NULL) return ESP_OK;

    esp_err_t err = nearby_board_spi2_init();
    if (err != ESP_OK) return err;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = NEARBY_BOARD_SPI_HOST;
    sdspi_device_config_t slot_config = sd_device_config();
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = SD_ALLOC_UNIT_BYTES,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    err = esp_vfs_fat_sdspi_mount(NEARBY_BOARD_SD_MOUNT_POINT,
                                  &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) s_card = NULL;
    return err;
}

esp_err_t nearby_board_sd_unmount(void)
{
    if (s_card == NULL) return ESP_OK;
    sdmmc_card_t *card = s_card;
    s_card = NULL;
    return esp_vfs_fat_sdcard_unmount(NEARBY_BOARD_SD_MOUNT_POINT, card);
}

bool nearby_board_sd_is_mounted(void) { return s_card != NULL; }
sdmmc_card_t *nearby_board_sd_card(void) { return s_card; }

esp_err_t nearby_board_sd_format_fat(void)
{
    if (s_card == NULL) return ESP_ERR_INVALID_STATE;
    return esp_vfs_fat_sdcard_format(NEARBY_BOARD_SD_MOUNT_POINT, s_card);
}

static esp_err_t raw_card_open(sdmmc_card_t *card, sdspi_dev_handle_t *device)
{
    if (card == NULL || device == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nearby_board_spi2_init();
    if (err != ESP_OK) return err;
    err = sdspi_host_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    sdspi_device_config_t device_config = sd_device_config();
    err = sdspi_host_init_device(&device_config, device);
    if (err != ESP_OK) return err;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = *device;
    memset(card, 0, sizeof(*card));
    err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) (void)sdspi_host_remove_device(*device);
    return err;
}

static esp_err_t erase_partition_metadata(sdmmc_card_t *card)
{
    if (card == NULL || card->csd.sector_size != 512 ||
        card->csd.capacity <= PARTITION_METADATA_HEAD + PARTITION_METADATA_TAIL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t zero_sector[512] __attribute__((aligned(4))) = {0};
    format_stage(NEARBY_BOARD_SD_FORMAT_ERASE_HEAD);
    for (size_t sector = 0; sector < PARTITION_METADATA_HEAD; ++sector) {
        esp_err_t err = sdmmc_write_sectors(card, zero_sector, sector, 1);
        if (err != ESP_OK) return err;
    }

    format_stage(NEARBY_BOARD_SD_FORMAT_ERASE_TAIL);
    const size_t tail_start = card->csd.capacity - PARTITION_METADATA_TAIL;
    for (size_t sector = tail_start; sector < card->csd.capacity; ++sector) {
        esp_err_t err = sdmmc_write_sectors(card, zero_sector, sector, 1);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t nearby_board_sd_format_whole(bool confirmed)
{
    if (!confirmed) return ESP_ERR_INVALID_STATE;
    s_format_error = ESP_OK;

    format_stage(NEARBY_BOARD_SD_FORMAT_UNMOUNT);
    esp_err_t err = nearby_board_sd_unmount();
    if (err != ESP_OK) return format_fail(err);

    format_stage(NEARBY_BOARD_SD_FORMAT_RAW_OPEN);
    sdspi_dev_handle_t device = -1;
    sdmmc_card_t raw_card;
    err = raw_card_open(&raw_card, &device);
    if (err != ESP_OK) return format_fail(err);

    err = erase_partition_metadata(&raw_card);
    format_stage(NEARBY_BOARD_SD_FORMAT_RAW_CLOSE);
    esp_err_t remove_err = sdspi_host_remove_device(device);
    if (err == ESP_OK && remove_err != ESP_OK) err = remove_err;
    if (err != ESP_OK) return format_fail(err);

    format_stage(NEARBY_BOARD_SD_FORMAT_REMOUNT_FAT);
    err = nearby_board_sd_mount(true);
    if (err != ESP_OK) return format_fail(err);

    format_stage(NEARBY_BOARD_SD_FORMAT_COMPLETE);
    ESP_LOGI(TAG, "whole-SD format complete");
    return ESP_OK;
}
''')

Path("firmware/components/db_storage/include/db_storage.h").write_text(r'''#pragma once

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
''')

Path("firmware/components/db_storage/db_storage.c").write_text(r'''#include "db_storage.h"

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
    if (s_part_file != NULL || s_prepared || s_format_task != NULL) return ESP_ERR_INVALID_STATE;
    s_preflight_safe_to_format = safe_to_format;
    if (!safe_to_format && s_format_state != NEARBY_DB_FORMAT_RUNNING) {
        s_format_state = NEARBY_DB_FORMAT_IDLE;
        s_format_error = ESP_OK;
    }
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
    if (xTaskCreate(format_worker, "db_format", FORMAT_TASK_STACK, NULL,
                    FORMAT_TASK_PRIORITY, &task) != pdPASS) {
        (void)esp_timer_stop(s_format_timeout_timer);
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    s_format_task = task;
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
''')

replace_once("firmware/components/db_storage/CMakeLists.txt",
             "REQUIRES nearby_board scan_session fatfs",
             "REQUIRES nearby_board scan_session fatfs freertos esp_timer")
