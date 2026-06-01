#include "global.h"

static const char *TAG_MAX = "MAX";

#define MAX6675_CONVERSION_TIME_MS 250

bool max_check(int64_t max_start) {
    bool buffer_full  = sys_data_g.max_sample >= MAX_SAMPLES;
    bool time_elapsed = (esp_timer_get_time() - max_start) >= (MAX_ACQ_DURATION_MS * 1000);

    if ((xEventGroupGetBits(xStatusEvent) & ACQUIRE)) {
        if (buffer_full || time_elapsed) {
            status_event_t evt = EVT_MAX_DONE;
            xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
            ESP_LOGI(TAG_MAX, "MAX acquisition stopped: %s", buffer_full ? "buffer full" : "time elapsed");

            return true;
        }
    }

    return false;
}

static esp_err_t max_init(max6675_handle_t *max_handle, gpio_num_t cs_num) {
    esp_err_t err;

    /* MAX6675 struct setup */
    max6675_config_t max_cfg = {
        .spi_host = SPI_HOST,
        .cs       = cs_num,
    };

    /* MAX initialization */
    err = max6675_init(&max_cfg, max_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MAX, "MAX init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG_MAX, "MAX (gpio: %d) initialized", cs_num);
    return ESP_OK;
}

void task_max(void *pvParameters) {
    esp_err_t err;

    max6675_handle_t max1_handle;
    max6675_handle_t max2_handle;

    uint16_t current_temperature1_raw;
    uint16_t current_temperature2_raw;

    err = max_init(&max1_handle, MAX1_CS);
    if (err != ESP_OK) {
        goto setup_error;
    }

    err = max_init(&max2_handle, MAX2_CS);
    if (err != ESP_OK) {
        goto setup_error;
    }

    /* MAX initialized -> Wait for acquisition to start */
    xEventGroupSetBits(xInitEvent, MAX_INIT);
    xEventGroupWaitBits(xStatusEvent, ACQUIRE, pdFALSE, pdTRUE, portMAX_DELAY);

    int64_t max_start = esp_timer_get_time();

    while (true) {

        if (max_check(max_start))
            break;

        /* MAX readings */
        max6675_read(max1_handle, &current_temperature1_raw);
        max6675_read(max2_handle, &current_temperature2_raw);

        /* Create MAX sample */
        max_data_t sample = {
            .timestamp        = (uint32_t)esp_timer_get_time(),
            .temperature1_raw = current_temperature1_raw,
            .temperature2_raw = current_temperature2_raw,
        };

        /* Copy sample to PSRAM */
        memcpy(&max_data_g[sys_data_g.max_sample], &sample, sizeof(max_data_t));
        sys_data_g.max_sample++;

        /* CS HIGH = measure - conversion time (CT_MS) -> CS LOW = output */
        vTaskDelay(pdMS_TO_TICKS(MAX6675_CONVERSION_TIME_MS));
    }

cleanup:
    max6675_delete(max1_handle);
    max6675_delete(max2_handle);

    vTaskDelete(NULL);

setup_error: {
    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
}

    goto cleanup;
}