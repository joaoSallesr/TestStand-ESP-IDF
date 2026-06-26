#include "global.h"

static const char *TAG_ADS = "ADS";

#define ADS_DRDY_TIMEOUT_MS 1
#define ADS_INIT_TIMEOUT_MS 1000

static void IRAM_ATTR drdy_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xTaskAds, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

bool ads_check(int64_t ads_start) {
    bool buffer_full  = sys_data_g.ads_sample >= ADS_SAMPLES;
    bool time_elapsed = (esp_timer_get_time() - ads_start) >= (ADS_ACQ_DURATION_MS * 1000);

    if ((xEventGroupGetBits(xStatusEventGroup) & ACQUIRE)) {
        if (buffer_full || time_elapsed) {
            status_event_t evt = EVT_ADS_DONE;
            xQueueSend(xEventQueue, &evt, portMAX_DELAY);
            ESP_LOGI(TAG_ADS, "ADS acquisition stopped: %s", buffer_full ? "buffer full" : "time elapsed");

            return true;
        }
    }

    return false;
}

static esp_err_t loadcell_init(ads1256_handle_t *loadcell_handle) {
    esp_err_t err;

    /* Load Cell struct setup */
    ads1256_config_t loadcell_cfg = {
        .spi_host        = SPI_HOST,
        .cs              = LOADCELL_CS,
        .drdy            = LOADCELL_DRDY,
        .gain            = ADS1256_GAIN_1,
        .drate           = ADS1256_DRATE_1000SPS,
        .pos_channel     = ADS1256_MUX_AIN0,
        .neg_channel     = ADS1256_MUX_AIN1,
        .drdy_timeout_ms = ADS_INIT_TIMEOUT_MS,
        .bufen           = false,
    };

    /* Load Cell initialization */
    err = ads1256_init(&loadcell_cfg, loadcell_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ADS, "ADS1 init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG_ADS, "ADS1 initialized");
    return ESP_OK;
}

static esp_err_t transducer_init(ads1256_handle_t *trans_handle) {
    esp_err_t err;

    /* Pressure Transducer struct setup */
    ads1256_config_t trans_cfg = {
        .spi_host        = SPI_HOST,
        .cs              = TRANS_CS,
        .drdy            = TRANS_DRDY,
        .gain            = ADS1256_GAIN_1,
        .drate           = ADS1256_DRATE_1000SPS,
        .pos_channel     = ADS1256_MUX_AIN0,
        .neg_channel     = ADS1256_MUX_AIN1,
        .drdy_timeout_ms = ADS_INIT_TIMEOUT_MS,
        .bufen           = false,
    };

    /* Pressure Transducer initialization */
    err = ads1256_init(&trans_cfg, trans_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ADS, "ADS2 init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG_ADS, "ADS2 initialized");
    return ESP_OK;
}

void task_ads(void *pvParameters) {
    esp_err_t err;

    ads1256_handle_t loadcell_handle;
    ads1256_handle_t transducer_handle;

    int32_t current_thrust_raw;
    int32_t current_pressure_raw;

    err = loadcell_init(&loadcell_handle);
    if (err != ESP_OK) {
        goto setup_error;
    }

    err = transducer_init(&transducer_handle);
    if (err != ESP_OK) {
        goto setup_error;
    }

    /* ADS DRDY ISR initialization */
    err = gpio_isr_handler_add(LOADCELL_DRDY, drdy_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ADS, "DRDY ISR failed: %s", esp_err_to_name(err));
        goto setup_error;
    }

    /* ADS initialized -> Wait for acquisition to start */
    xEventGroupSetBits(xInitEventGroup, ADS_INIT);
    xEventGroupWaitBits(xStatusEventGroup, ACQUIRE, pdFALSE, pdTRUE, portMAX_DELAY);

    int64_t ads_start = esp_timer_get_time();

    while (true) {

        if (ads_check(ads_start))
            break;

        /* ADS Synchronization */
        gpio_set_level(LOADCELL_SYNC, LOW);
        gpio_set_level(TRANS_SYNC, LOW);
        ets_delay_us(ADS1256_T11_SYNC_US);
        gpio_set_level(LOADCELL_SYNC, HIGH);
        gpio_set_level(TRANS_SYNC, HIGH);

        /* Wait DRDY */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ADS_DRDY_TIMEOUT_MS));

        /* Load Cell + Pressure Transducer reading */
        // err = ads1256_read_result(loadcell_handle, &current_thrust_raw);
        err = ads1256_read_continuous(loadcell_handle, &current_thrust_raw);
        if (err != ESP_OK)
            sys_data_g.ads_lost++;

        // err = ads1256_read_result(transducer_handle, &current_pressure_raw);
        err = ads1256_read_continuous(transducer_handle, &current_pressure_raw);
        if (err != ESP_OK)
            sys_data_g.ads_lost++;

        /* Create ads sample */
        ads_data_t sample = {
            .timestamp    = (uint32_t)esp_timer_get_time(),
            .thrust_raw   = current_thrust_raw,
            .pressure_raw = current_pressure_raw,
        };

        /* Copy sample to PSRAM */
        memcpy(&ads_data_g[sys_data_g.ads_sample], &sample, sizeof(ads_data_t));
        sys_data_g.ads_sample++;
    }

    ESP_LOGE(TAG_ADS, "Lost Samples/Read Samples : %d/%d", sys_data_g.ads_lost, sys_data_g.ads_sample);

cleanup:
    ads1256_send_cmd(loadcell_handle, ADS1256_CMD_SDATAC);
    ads1256_send_cmd(transducer_handle, ADS1256_CMD_SDATAC);

    ads1256_delete(loadcell_handle);
    ads1256_delete(transducer_handle);

    gpio_intr_disable(LOADCELL_DRDY);
    gpio_isr_handler_remove(LOADCELL_DRDY);

    vTaskDelete(NULL);

setup_error: {
    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);
}
    goto cleanup;
}