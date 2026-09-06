#pragma once

#include "esp_err.h"
#include "nearby_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin LVGL v8 board binding only. D remains the owner of ST7789/CST816/SPI/I2C
 * lifecycle. The product owner task must be the only caller of lv_timer_handler().
 */
esp_err_t nearby_lvgl_port_init(const nearby_board_lcd_handles_t *lcd);

#ifdef __cplusplus
}
#endif
