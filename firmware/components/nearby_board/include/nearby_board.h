#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"

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

#define NEARBY_BOARD_I2C_SCL_GPIO      8
#define NEARBY_BOARD_I2C_SDA_GPIO      18
#define NEARBY_BOARD_CST816_ADDR       0x15

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
} nearby_board_lcd_handles_t;

/** Initialize the one shared SPI2 bus used by LCD and SD.
 *
 * This function includes SD MISO on the bus from the start. Call once during
 * board bring-up; LCD and SDSPI devices must attach to this same host.
 */
esp_err_t nearby_board_spi2_init(void);

/** Bring up the 170x320 ST7789V2 panel using the ESP-IDF public ST7789 driver.
 *
 * Applies the verified 35-column controller offset so callers use logical
 * coordinates 0..169 x 0..319 without LVGL-side +35 hacks.
 */
esp_err_t nearby_board_lcd_init(nearby_board_lcd_handles_t *out_handles);

/** Backlight is active-low on this board's high-side switch. */
esp_err_t nearby_board_backlight_set(bool on);

#ifdef __cplusplus
}
#endif
