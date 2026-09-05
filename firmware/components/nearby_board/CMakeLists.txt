idf_component_register(
    SRCS "nearby_board.c" "nearby_touch.c" "nearby_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_i2c esp_driver_sdspi fatfs sdmmc
)
