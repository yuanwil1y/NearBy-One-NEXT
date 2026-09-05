#include "nearby_board.h"

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"

#define NEARBY_BOARD_I2C_PORT          I2C_NUM_0
#define NEARBY_BOARD_I2C_CLOCK_HZ      200000
#define NEARBY_BOARD_I2C_TIMEOUT_TICKS pdMS_TO_TICKS(100)

static bool s_i2c_initialized;
static bool s_cst816_initialized;

esp_err_t nearby_board_i2c_init(void)
{
    if (s_i2c_initialized) return ESP_OK;

    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = NEARBY_BOARD_I2C_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = NEARBY_BOARD_I2C_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = NEARBY_BOARD_I2C_CLOCK_HZ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(NEARBY_BOARD_I2C_PORT, &config);
    if (err != ESP_OK) return err;
    err = i2c_driver_install(NEARBY_BOARD_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_i2c_initialized = true;
        return ESP_OK;
    }
    return err;
}

esp_err_t nearby_board_cst816_init(void)
{
    esp_err_t err = nearby_board_i2c_init();
    if (err != ESP_OK) return err;

    const uint8_t command[2] = {0x00, 0x00};
    err = i2c_master_write_to_device(NEARBY_BOARD_I2C_PORT,
                                     NEARBY_BOARD_CST816_ADDR,
                                     command,
                                     sizeof(command),
                                     NEARBY_BOARD_I2C_TIMEOUT_TICKS);
    if (err == ESP_OK) s_cst816_initialized = true;
    return err;
}

esp_err_t nearby_board_cst816_read(nearby_board_touch_sample_t *out_sample)
{
    if (out_sample == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_cst816_initialized) {
        esp_err_t err = nearby_board_cst816_init();
        if (err != ESP_OK) return err;
    }

    uint8_t start_register = 0x00;
    uint8_t data[7] = {0};
    esp_err_t err = i2c_master_write_read_device(NEARBY_BOARD_I2C_PORT,
                                                  NEARBY_BOARD_CST816_ADDR,
                                                  &start_register,
                                                  1,
                                                  data,
                                                  sizeof(data),
                                                  NEARBY_BOARD_I2C_TIMEOUT_TICKS);
    if (err != ESP_OK) return err;

    out_sample->pressed = data[2] != 0;
    out_sample->x = 0;
    out_sample->y = 0;
    if (out_sample->pressed) {
        out_sample->x = (uint16_t)(((uint16_t)(data[3] & 0x0f) << 8) | data[4]);
        out_sample->y = (uint16_t)(((uint16_t)(data[5] & 0x0f) << 8) | data[6]);
    }
    return ESP_OK;
}
