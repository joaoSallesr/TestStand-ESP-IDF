#include "max6675.h"

static const char *TAG = "max6675";

#define MAX6675_SPI_CLK_HZ 4300000

#define ESP_ARG_CHECK(VAL)                                                                                             \
    do {                                                                                                               \
        if (!(VAL))                                                                                                    \
            return ESP_ERR_INVALID_ARG;                                                                                \
    } while (0)

static inline void cs_low(max6675_handle_t handle) { gpio_set_level(handle->dev_config.cs, 0); }

static inline void cs_high(max6675_handle_t handle) { gpio_set_level(handle->dev_config.cs, 1); }

esp_err_t max6675_init(const max6675_config_t *max6675_config, max6675_handle_t *max6675_handle) {
    ESP_ARG_CHECK(max6675_config);
    esp_err_t ret = ESP_FAIL;

    /* validate memory allocation */
    max6675_handle_t out_handle = (max6675_handle_t)calloc(1, sizeof(*out_handle));
    ESP_RETURN_ON_FALSE(out_handle, ESP_ERR_NO_MEM, TAG, "No memory for MAX6675 device");

    /* copy device config to out_handle */
    out_handle->dev_config = *max6675_config;

    /* configure CS pin as output, idle high */
    gpio_config_t cs_conf = {
        .pin_bit_mask = 1ULL << max6675_config->cs,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_GOTO_ON_ERROR(gpio_config(&cs_conf), err_handle, TAG, "Failed to configure CS pin");
    gpio_set_level(max6675_config->cs, 1); // idle high

    /* SPI device configuration */
    const spi_device_interface_config_t max_dev_config = {
        .mode           = 0,                  // CPOL=0 (idle low), CPHA=0 (sample on rising edge)
        .clock_speed_hz = MAX6675_SPI_CLK_HZ, // 4.3 MHz
        .spics_io_num   = -1,                 // CS managed manually
        .queue_size     = 1,                  // queue not used
    };

    /* add MAX6675 to SPI bus */
    ESP_GOTO_ON_ERROR(spi_bus_add_device(max6675_config->spi_host, &max_dev_config, &out_handle->spi_handle),
                      err_handle, TAG, "Failed to add ADS1256 device to SPI bus");

    *max6675_handle = out_handle;
    return ESP_OK;

err_handle:
    if (out_handle->spi_handle)
        spi_bus_remove_device(out_handle->spi_handle);
    free(out_handle);
    return ret;
}

esp_err_t max6675_read(max6675_handle_t handle, uint16_t *out_raw) {
    esp_err_t ret = ESP_OK;
    uint16_t  data;

    // 16 bits reading
    spi_transaction_t t = {
        .length    = 16,
        .rxlength  = 16,
        .rx_buffer = &data,
    };

    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(handle->spi_handle, portMAX_DELAY), TAG,
                        "Failed to acquire bus for read");

    cs_low(handle);
    ESP_GOTO_ON_ERROR(spi_device_polling_transmit(handle->spi_handle, &t), done, TAG, "Failed to transmit reading");

done:
    cs_high(handle);
    spi_device_release_bus(handle->spi_handle);

    if (ret != ESP_OK)
        return ret;

    /* get the temperature bits (14:3) from data */
    uint16_t raw = (data >> 3) & 0x0FFF;

    /* temperature resolution = 0.25 */
    raw *= 0.25;
    *out_raw = raw;

    return ESP_OK;
}

esp_err_t max6675_delete(max6675_handle_t handle) {
    ESP_ARG_CHECK(handle);

    if (handle->spi_handle) {
        ESP_RETURN_ON_ERROR(spi_bus_remove_device(handle->spi_handle), TAG, "Failed to remove SPI device");
    }
    free(handle);
    return ESP_OK;
}
