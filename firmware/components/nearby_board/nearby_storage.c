#include "nearby_board.h"

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
