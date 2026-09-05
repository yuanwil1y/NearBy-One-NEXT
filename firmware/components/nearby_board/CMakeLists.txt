idf_component_register(
    SRCS "nearby_board.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio
)
