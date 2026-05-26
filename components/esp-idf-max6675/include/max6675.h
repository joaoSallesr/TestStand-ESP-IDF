#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <rom/ets_sys.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

/* MAX6675 structures */

/**
 * @brief MAX6675 configuration structure.
 */
typedef struct max6675_config_s {
    spi_host_device_t spi_host; /*!< SPI bus selected */
    gpio_num_t        cs;       /*!< max6675 CS pin */
} max6675_config_t;

/**
 * @brief MAX6675 context structure.
 */
typedef struct max6675_context_s {
    max6675_config_t    dev_config;
    spi_device_handle_t spi_handle;
} max6675_context_t;

/**
 * @brief MAX6675 handle definition.
 */
typedef max6675_context_t *max6675_handle_t;

esp_err_t max6675_init(const max6675_config_t *max6675_config, max6675_handle_t *max6675_handle);
esp_err_t max6675_read(max6675_handle_t handle, uint16_t *out_raw);
esp_err_t max6675_delete(max6675_handle_t handle);
