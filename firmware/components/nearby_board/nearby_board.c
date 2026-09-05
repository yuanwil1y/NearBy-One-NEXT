#include "nearby_board.h"

#include "driver/gpio.h"
#include "esp_lcd_panel_st7789.h"

#define LCD_PIXEL_CLOCK_HZ      (20 * 1000 * 1000)
#define LCD_SPI_QUEUE_DEPTH     10
#define LCD_MAX_TRANSFER_BYTES  (NEARBY_BOARD_LCD_H_RES * 40 * sizeof(uint16_t))

static bool s_spi2_initialized;

esp_err_t nearby_board_spi2_init(void)
{
    if (s_spi2_initialized) {
        return ESP_OK;
    }

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = NEARBY_BOARD_LCD_MOSI_GPIO,
        .miso_io_num = NEARBY_BOARD_SD_MISO_GPIO,
        .sclk_io_num = NEARBY_BOARD_LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_MAX_TRANSFER_BYTES,
    };

    esp_err_t err = spi_bus_initialize(NEARBY_BOARD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    s_spi2_initialized = true;
    return ESP_OK;
}

esp_err_t nearby_board_backlight_set(bool on)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << NEARBY_BOARD_LCD_BL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    /* Q2 is a high-side P-channel switch: GPIO low turns LCD backlight on. */
    return gpio_set_level(NEARBY_BOARD_LCD_BL_GPIO, on ? 0 : 1);
}

esp_err_t nearby_board_lcd_init(nearby_board_lcd_handles_t *out_handles)
{
    if (out_handles == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_handles->io = NULL;
    out_handles->panel = NULL;

    esp_err_t err = nearby_board_backlight_set(false);
    if (err != ESP_OK) {
        return err;
    }

    err = nearby_board_spi2_init();
    if (err != ESP_OK) {
        return err;
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = NEARBY_BOARD_LCD_DC_GPIO,
        .cs_gpio_num = NEARBY_BOARD_LCD_CS_GPIO,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = LCD_SPI_QUEUE_DEPTH,
    };

    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)NEARBY_BOARD_SPI_HOST,
                                   &io_cfg,
                                   &out_handles->io);
    if (err != ESP_OK) {
        return err;
    }

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = NEARBY_BOARD_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    err = esp_lcd_new_panel_st7789(out_handles->io, &panel_cfg, &out_handles->panel);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_lcd_panel_reset(out_handles->panel);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_lcd_panel_init(out_handles->panel);
    if (err != ESP_OK) {
        return err;
    }

    /* The 170px visible area is centered inside the ST7789's 240px RAM. */
    err = esp_lcd_panel_set_gap(out_handles->panel,
                                NEARBY_BOARD_LCD_X_GAP,
                                NEARBY_BOARD_LCD_Y_GAP);
    if (err != ESP_OK) {
        return err;
    }

    /* Waveshare's working init sequence enables display inversion (0x21). */
    err = esp_lcd_panel_invert_color(out_handles->panel, true);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_lcd_panel_disp_on_off(out_handles->panel, true);
    if (err != ESP_OK) {
        return err;
    }

    return nearby_board_backlight_set(true);
}
