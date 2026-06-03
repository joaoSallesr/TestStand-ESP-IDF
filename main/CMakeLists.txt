idf_component_register(SRCS main.c
                            global.c
                            system.c
                            ignite.c
                            sensors/ads.c
                            sensors/max.c
                            save_send.c
                        INCLUDE_DIRS "."
                        REQUIRES
                            esp_ads1256
                            esp-idf-max6675
                            esp-idf-sx126x
                            esp_littlefs-1.22.1
                            esp_psram
                            esp_ringbuf
                            esp_driver_gpio
                            esp_driver_spi
                            esp_driver_sdmmc
                            sdmmc
                            fatfs
                            vfs
                            nvs_flash
                            esp_timer)
