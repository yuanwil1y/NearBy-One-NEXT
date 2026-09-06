#include "nearby_board.h"

#include <string.h>

#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"

#define SD_MAX_FILES             6
#define SD_ALLOC_UNIT_BYTES      4096
#define PARTITION_METADATA_HEAD  34U
#define PARTITION_METADATA_TAIL  33U

static sdmmc_card_t *s_card;

static sdspi_device_config_t sd_device_config(void)
{
    sdspi_device_config_t config = SDSPI_DEVICE_CONFIG_DEFAULT();
    config.host_id = NEARBY_BOARD_SPI_HOST;
    config.gpio_cs = NEARBY_BOARD_SD_CS_GPIO;
    return config;
}

esp_err_t nearby_board_sd_mount(bool format_if_mount_failed)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    esp_err_t err = nearby_board_spi2_init();
    if (err != ESP_OK) {
        return err;
    }

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
                                  &host,
                                  &slot_config,
                                  &mount_config,
                                  &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
    }
    return err;
}

esp_err_t nearby_board_sd_unmount(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }

    sdmmc_card_t *card = s_card;
    s_card = NULL;
    /* This detaches the SDSPI device. SPI2 itself remains owned by the BSP. */
    return esp_vfs_fat_sdcard_unmount(NEARBY_BOARD_SD_MOUNT_POINT, card);
}

bool nearby_board_sd_is_mounted(void)
{
    return s_card != NULL;
}

sdmmc_card_t *nearby_board_sd_card(void)
{
    return s_card;
}

esp_err_t nearby_board_sd_format_fat(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_vfs_fat_sdcard_format(NEARBY_BOARD_SD_MOUNT_POINT, s_card);
}

static esp_err_t raw_card_open(sdmmc_card_t *card, sdspi_dev_handle_t *device)
{
    if (card == NULL || device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nearby_board_spi2_init();
    if (err != ESP_OK) {
        return err;
    }

    err = sdspi_host_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    sdspi_device_config_t device_config = sd_device_config();
    err = sdspi_host_init_device(&device_config, device);
    if (err != ESP_OK) {
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = *device;
    memset(card, 0, sizeof(*card));
    err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        (void)sdspi_host_remove_device(*device);
    }
    return err;
}

static esp_err_t erase_partition_metadata(sdmmc_card_t *card)
{
    if (card == NULL || card->csd.sector_size != 512 ||
        card->csd.capacity <= PARTITION_METADATA_HEAD + PARTITION_METADATA_TAIL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t zero_sector[512] __attribute__((aligned(4))) = {0};

    for (size_t sector = 0; sector < PARTITION_METADATA_HEAD; ++sector) {
        esp_err_t err = sdmmc_write_sectors(card, zero_sector, sector, 1);
        if (err != ESP_OK) {
            return err;
        }
    }

    const size_t tail_start = card->csd.capacity - PARTITION_METADATA_TAIL;
    for (size_t sector = tail_start; sector < card->csd.capacity; ++sector) {
        esp_err_t err = sdmmc_write_sectors(card, zero_sector, sector, 1);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t nearby_board_sd_format_whole(bool confirmed)
{
    if (!confirmed) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nearby_board_sd_unmount();
    if (err != ESP_OK) {
        return err;
    }

    sdspi_dev_handle_t device = -1;
    sdmmc_card_t raw_card;
    err = raw_card_open(&raw_card, &device);
    if (err != ESP_OK) {
        return err;
    }

    err = erase_partition_metadata(&raw_card);
    esp_err_t remove_err = sdspi_host_remove_device(device);
    if (err == ESP_OK && remove_err != ESP_OK) {
        err = remove_err;
    }
    if (err != ESP_OK) {
        return err;
    }

    /* With partition metadata gone, ESP-IDF creates one 100% partition. */
    return nearby_board_sd_mount(true);
}
