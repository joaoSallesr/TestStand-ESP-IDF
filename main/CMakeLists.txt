cmake_minimum_required(VERSION 4.0)

list(APPEND SOURCES main.c global.c system.c ignite.c sensors/ads.c sensors/max.c save.c)

list(APPEND REQUIRES esp_ads1256 esp-idf-max6675 esp_littlefs-1.22.1 
                        esp_psram esp_driver_gpio esp_driver_spi 
                        esp_driver_sdmmc sdmmc fatfs vfs nvs_flash esp_timer)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES ${REQUIRES}
)
