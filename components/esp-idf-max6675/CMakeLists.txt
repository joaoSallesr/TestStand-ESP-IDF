idf_component_register(
    SRCS "max6675.c"
    REQUIRES esp_common esp_driver_gpio esp_driver_spi esp_rom esp_system esp_timer freertos log
    INCLUDE_DIRS "include"
)
