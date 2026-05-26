#include "global.h"

static const char *TAG_ADS = "ADS";

#define FULL_ACQ_DURATION_MS 7000
#define ADS_DRDY_TIMEOUT_MS  10

static void IRAM_ATTR drdy_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xTaskAds, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

bool ads_check(int64_t ads_start) {
    bool buffer_full  = sys_data_g.ads_sample >= ADS_SAMPLES;
    bool time_elapsed = (esp_timer_get_time() - ads_start) >= (FULL_ACQ_DURATION_MS * 1000);

    if ((xEventGroupGetBits(xSystemEvent) & FULL_ACQ)) {
        if (buffer_full || time_elapsed) {
            sys_event_t evt = EVT_ADS_DONE;
            xQueueSend(xEventQueue, &evt, portMAX_DELAY);
            ESP_LOGI(TAG_ADS, "Full acquisition stopped: %s", buffer_full ? "buffer full" : "time elapsed");

            return true;
        }
    }

    return false;
}

static void loadcell_init(ads1256_handle_t *loadcell_handle) {
    /* Load Cell struct setup */
    ads1256_config_t loadcell_cfg = {
        .spi_host        = SPI_HOST,
        .cs              = LOADCELL_CS,
        .drdy            = LOADCELL_DRDY,
        .gain            = ADS1256_GAIN_1,
        .drate           = ADS1256_DRATE_1000SPS,
        .pos_channel     = ADS1256_MUX_AIN0,
        .neg_channel     = ADS1256_MUX_AIN1,
        .drdy_timeout_ms = ADS_DRDY_TIMEOUT_MS,
        .bufen           = false,
    };

    /* Load Cell initialization */
    ESP_ERROR_CHECK(ads1256_init(&loadcell_cfg, loadcell_handle));

    ESP_LOGI(TAG_ADS, "ADS1 initialized");
}

static void transducer_init(ads1256_handle_t *trans_handle) {
    /* Pressure Transducer struct setup */
    ads1256_config_t trans_cfg = {
        .spi_host        = SPI_HOST,
        .cs              = TRANS_CS,
        .drdy            = TRANS_DRDY,
        .gain            = ADS1256_GAIN_1,
        .drate           = ADS1256_DRATE_1000SPS,
        .pos_channel     = ADS1256_MUX_AIN0,
        .neg_channel     = ADS1256_MUX_AIN1,
        .drdy_timeout_ms = ADS_DRDY_TIMEOUT_MS,
        .bufen           = false,
    };

    /* Pressure Transducer initialization */
    ESP_ERROR_CHECK(ads1256_init(&trans_cfg, trans_handle));

    ESP_LOGI(TAG_ADS, "ADS2 initialized");
}

void task_ads(void *pvParameters) {
    ads1256_handle_t loadcell_handle;
    ads1256_handle_t transducer_handle;

    int32_t current_loadcell;
    int32_t current_transducer;

    loadcell_init(&loadcell_handle);
    transducer_init(&transducer_handle);

    /* ADS DRDY ISR initialization */
    ESP_ERROR_CHECK(gpio_isr_handler_add(LOADCELL_DRDY, drdy_isr_handler, NULL));

    /* Wait for acquisition to start */
    xEventGroupWaitBits(xSystemEvent, FULL_ACQ, pdFALSE, pdTRUE, portMAX_DELAY);

    int64_t ads_start = esp_timer_get_time();

    while (true) {
        ads1256_start_conversion(loadcell_handle);
        ads1256_start_conversion(transducer_handle);

        /* Wait DRDY */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ADS_DRDY_TIMEOUT_MS));

        /* Load Cell + Pressure Transducer reading */
        ads1256_read_result(loadcell_handle, &current_loadcell);
        ads1256_read_result(transducer_handle, &current_transducer);

        /* Create ads sample */
        ads_data_t sample = {
            .timestamp = (uint32_t)esp_timer_get_time(),
            .loadcell  = current_loadcell,
            .trans     = current_transducer,
        };

        /* Copy sample to PSRAM */
        memcpy(&ads_data_g[sys_data_g.ads_sample], &sample, sizeof(ads_data_t));
        sys_data_g.ads_sample++;

        if (ads_check(ads_start))
            break;
    }

    ESP_ERROR_CHECK(ads1256_delete(loadcell_handle));
    ESP_ERROR_CHECK(ads1256_delete(transducer_handle));

    gpio_intr_disable(LOADCELL_DRDY);
    gpio_isr_handler_remove(LOADCELL_DRDY);

    vTaskDelete(NULL);
}