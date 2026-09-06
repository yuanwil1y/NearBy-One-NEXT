#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Waveshare ESP32-C6-Touch-LCD-1.9, verified against official schematic. */
#define NEARBY_BOARD_SPI_HOST          SPI2_HOST
#define NEARBY_BOARD_LCD_H_RES         170
#define NEARBY_BOARD_LCD_V_RES         320
#define NEARBY_BOARD_LCD_X_GAP         35
#define NEARBY_BOARD_LCD_Y_GAP         0

#define NEARBY_BOARD_LCD_MOSI_GPIO     4
#define NEARBY_BOARD_LCD_SCLK_GPIO     5
#define NEARBY_BOARD_LCD_DC_GPIO       6
#define NEARBY_BOARD_LCD_CS_GPIO       7
#define NEARBY_BOARD_LCD_RST_GPIO      14
#define NEARBY_BOARD_LCD_BL_GPIO       15

#define NEARBY_BOARD_SD_MISO_GPIO      19
#define NEARBY_BOARD_SD_CS_GPIO        20
#define NEARBY_BOARD_SD_MOUNT_POINT    "/sdcard"

#define NEARBY_BOARD_I2C_SCL_GPIO      8
#define NEARBY_BOARD_I2C_SDA_GPIO      18
#define NEARBY_BOARD_CST816_ADDR       0x15

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} nearby_board_lcd_handles_t;

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
} nearby_board_touch_sample_t;

/** Initialize the one shared SPI2 bus used by LCD and SD. */
esp_err_t nearby_board_spi2_init(void);

/** Bring up the 170x320 ST7789V2 panel with the 35-column logical gap. */
esp_err_t nearby_board_lcd_init(nearby_board_lcd_handles_t *out_handles);

/** Backlight is active-low on this board's high-side switch. */
esp_err_t nearby_board_backlight_set(bool on);

/** Initialize I2C0 once for CST816/QMI8658 board peripherals. */
esp_err_t nearby_board_i2c_init(void);

/** Put CST816 into normal mode using Waveshare's register-0 transaction. */
esp_err_t nearby_board_cst816_init(void);

/** Read one raw CST816 sample. Coordinate orientation is intentionally untouched. */
esp_err_t nearby_board_cst816_read(nearby_board_touch_sample_t *out_sample);

typedef enum {
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
esp_err_t nearby_board_sd_format_error(void);

/** Unmount FATFS and detach only the SD SPI device; never free SPI2 here. */
esp_err_t nearby_board_sd_unmount(void);

bool nearby_board_sd_is_mounted(void);
sdmmc_card_t *nearby_board_sd_card(void);

/** Reformat the currently mounted FAT filesystem. */
esp_err_t nearby_board_sd_format_fat(void);

/**
 * Destructive whole-card reset used by v0.1 DB replacement.
 *
 * Requires explicit `confirmed=true`, unmounts any existing filesystem,
 * destroys old MBR/GPT metadata with bounded raw sector writes, then remounts
 * with ESP-IDF's `format_if_mount_failed` path so one 100% FAT partition is
 * created. This is a destructive format, not a cryptographic secure erase.
 */
esp_err_t nearby_board_sd_format_whole(bool confirmed);

#ifdef __cplusplus
}
#endif
