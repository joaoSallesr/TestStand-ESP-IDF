#include "global.h"

static const char *TAG_MAX = "MAX";

#define MAX6675_CONVERSION_TIME_MS 250

bool max_check(int64_t max_start) {
    bool buffer_full  = sys_data_g.max_sample >= MAX_SAMPLES;
    bool time_elapsed = (esp_timer_get_time() - max_start) >= (PARTIAL_ACQ_DURATION_MS * 1000);

    if ((xEventGroupGetBits(xSystemEvent) & (FULL_ACQ | PART_ACQ))) {
        if (buffer_full || time_elapsed) {
            sys_event_t evt = EVT_MAX_DONE;
            xQueueSend(xEventQueue, &evt, portMAX_DELAY);
            ESP_LOGI(TAG_MAX, "Partial acquisition stopped: %s", buffer_full ? "buffer full" : "time elapsed");

            return true;
        }
    }

    return false;
}

static void max_init(max6675_handle_t *max_handle, gpio_num_t cs_num) {
    /* MAX6675 struct setup */
    max6675_config_t max_cfg = {
        .spi_host = SPI_HOST,
        .cs       = cs_num,
    };

    /* MAX initialization */
    ESP_ERROR_CHECK(max6675_init(&max_cfg, max_handle));
    ESP_LOGI(TAG_MAX, "MAX initialized");
}

void task_max(void *pvParameters) {
    max6675_handle_t max1_handle;
    max6675_handle_t max2_handle;

    uint16_t current_temperature1_raw;
    uint16_t current_temperature2_raw;

    max_init(&max1_handle, MAX1_CS);
    max_init(&max2_handle, MAX2_CS);

    /* Wait for acquisition to start */
    xEventGroupWaitBits(xSystemEvent, FULL_ACQ, pdFALSE, pdTRUE, portMAX_DELAY);

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

    ESP_ERROR_CHECK(max6675_delete(max1_handle));
    ESP_ERROR_CHECK(max6675_delete(max2_handle));

    vTaskDelete(NULL);
}