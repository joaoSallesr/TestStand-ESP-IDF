#include "global.h"

static const char *TAG_SD       = "SD";
static const char *TAG_LITTLEFS = "LittleFS";
static const char *TAG_NVS      = "NVS";
static const char *TAG_LORA     = "LoRa";

// SD & LITTLEFS CONFIG
#define SD_MAX_FILES    5
#define SD_MOUNT        "/sdcard"
#define SD_BUFFER_SIZE  32 * 1024
#define SD_UNIT_SIZE    32 * 1024
#define LFS_MAX_FILES   32
#define LFS_BUFFER_SIZE 512
#define FILENAME_LENGTH 32

// LORA CONFIG
#define LORA_FREQUENCY        915000000 // Hz
#define LORA_SPREADING_FACTOR 5
#define LORA_BANDWIDTH        SX126X_LORA_BW_125_0
#define LORA_CODING_RATE      SX126X_LORA_CR_4_5
#define LORA_DIO1_TIMEOUT_MS  1000

static void IRAM_ATTR dio1_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xTaskLora, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

void task_sd(void *pvParameters) {
    esp_err_t     err;
    sdmmc_card_t *card;
    uint8_t      *dma_buf = NULL;

    ESP_LOGI(TAG_SD, "Initializing SD card");

    /* SDIO host driver (4-bit mode enabled, max frequency set to 20MHz) */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    /* SDIO slot config */
    sdmmc_slot_config_t sd_cfg = {
        .clk     = SD_CLK,
        .cmd     = SD_CMD,
        .d0      = SD_DAT0,
        .d1      = SD_DAT1,
        .d2      = SD_DAT2,
        .d3      = SD_DAT3,
        .cd      = GPIO_NUM_NC,
        .gpio_wp = GPIO_NUM_NC,
        .width   = 4, // 4-bit mode
        .flags   = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
    };

    /* Options for mounting file system */
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = SD_MAX_FILES,
        .allocation_unit_size   = SD_UNIT_SIZE,
    };

    /* Mount filesystem */
    ESP_LOGI(TAG_SD, "Mounting filesystem");
    err = esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &sd_cfg, &mount_cfg, &card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG_SD, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG_SD, "Failed to initialize the card (%s).", esp_err_to_name(err));
        }

        goto error;
    }
    ESP_LOGI(TAG_SD, "Filesystem mounted");
    sdmmc_card_print_info(stdout, card);

    /* Allocate DMA-capable internal buffer */
    dma_buf = heap_caps_malloc(SD_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (dma_buf == NULL) {
        ESP_LOGE(TAG_SD, "Failed to allocate DMA buffer");
        err = ESP_ERR_NO_MEM;
        goto error;
    }

    /* SD initialized -> Wait for SAVE_DATA */
    xEventGroupSetBits(xInitEvent, SD_INIT);
    xEventGroupWaitBits(xStatusEvent, SAVE_DATA, pdFALSE, pdTRUE, portMAX_DELAY);

    uint32_t ads_total = sys_data_g.ads_sample;
    uint32_t max_total = sys_data_g.max_sample;
    ESP_LOGI(TAG_SD, "Saving %lu ADS samples, %lu MAX samples", ads_total, max_total);

    /* Create log file */
    char log_name[FILENAME_LENGTH];
    snprintf(log_name, FILENAME_LENGTH, "%s/test%lu.bin", SD_MOUNT, file_counter_g.sd_files);
    ESP_LOGI(TAG_SD, "Creating file %s", log_name);

    FILE *f = fopen(log_name, "wb");
    if (!f) {
        ESP_LOGE(TAG_SD, "Failed to open file for writing");
        goto cleanup;
    }

    /* Write header */
    file_header_t hdr = {
        .name_check  = 0xABCD1234,
        .ads_samples = ads_total,
        .max_samples = max_total,
        .timestamp   = (uint32_t)esp_timer_get_time(),
    };

    if (fwrite(&hdr, sizeof(file_header_t), 1, f) != 1) {
        ESP_LOGE(TAG_SD, "Failed to write header");
        goto close;
    }

    /* Write ADS data */
    {
        const size_t   sample_size = sizeof(ads_data_t);
        const uint32_t chunk       = SD_BUFFER_SIZE / sample_size;
        uint32_t       written     = 0;

        while (written < ads_total) {
            uint32_t batch = ((ads_total - written) < chunk ? (ads_total - written) : chunk); // sets chunk size
            size_t   bytes = batch * sample_size;

            memcpy(dma_buf, &ads_data_g[written], bytes);

            if (fwrite(dma_buf, sample_size, batch, f) != batch) {
                ESP_LOGE(TAG_SD, "ADS write error at sample %lu", written);
                goto close;
            }
            written += batch;
        }
        ESP_LOGI(TAG_SD, "ADS: %lu samples written (%lu bytes)", written, written * sample_size);
    }

    /* Write MAX data */
    {
        const size_t   sample_size = sizeof(max_data_t);
        const uint32_t chunk       = SD_BUFFER_SIZE / sample_size;
        uint32_t       written     = 0;

        while (written < max_total) {
            uint32_t batch = ((max_total - written) < chunk ? (max_total - written) : chunk); // sets chunk size
            size_t   bytes = batch * sample_size;

            memcpy(dma_buf, &max_data_g[written], bytes);

            if (fwrite(dma_buf, sample_size, batch, f) != batch) {
                ESP_LOGE(TAG_SD, "MAX write error at sample %lu", written);
                goto close;
            }
            written += batch;
        }
        ESP_LOGI(TAG_SD, "MAX: %lu samples written (%lu bytes)", written, written * sample_size);
    }

    ESP_LOGI(TAG_SD, "SAVE_DATA complete");

close:
    fclose(f);

cleanup:
    free(dma_buf);

    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    ESP_LOGI(TAG_SD, "Card unmounted");

    EventBits_t bits = xEventGroupSetBits(xStatusEvent, SD_DONE);
    if ((bits & (SD_DONE | LFS_DONE)) == (SD_DONE | LFS_DONE)) {
        status_event_t evt = EVT_SAVE_DONE;
        xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
    }

    vTaskDelete(NULL);

error:
    ESP_LOGE(TAG_SD, "SD init failed: %s", esp_err_to_name(err));

    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);
}

void task_lfs(void *pvParameters) {

    // TODO:
    // LFS INIT

    /* Wait for SAVE_DATA */
    xEventGroupWaitBits(xStatusEvent, SAVE_DATA, pdFALSE, pdTRUE, portMAX_DELAY);

    // TODO:
    // LFS SAVE

    EventBits_t bits = xEventGroupSetBits(xStatusEvent, LFS_DONE);
    if ((bits & (SD_DONE | LFS_DONE)) == (SD_DONE | LFS_DONE)) {
        status_event_t evt = EVT_SAVE_DONE;
        xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
    }

    vTaskDelete(NULL);

    // error:
    // ESP_LOGE(TAG_LITTLEFS, "LFS init failed: %s", esp_err_to_name(err));

    // status_event_t evt = EVT_SETUP_FAILED;
    // xQueueSend(xStatusQueue, &evt, portMAX_DELAY);

    // vTaskDelete(NULL);
}

void task_nvs(void *pvParameters) {
    nvs_handle_t nvs_handle;

    /* Wait for SAVE_DONE */
    xEventGroupWaitBits(xStatusEvent, NVS_EDIT, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG_NVS, "Starting NVS file counter update");
    nvs_open("storage", NVS_READWRITE, &nvs_handle);

    /* Increment file counter */
    file_counter_g.sd_files += 1;
    file_counter_g.lfs_files += 1;

    /* Update NVS */
    nvs_set_u32(nvs_handle, "sd_counter", file_counter_g.sd_files);
    nvs_set_u32(nvs_handle, "lfs_counter", file_counter_g.lfs_files);
    nvs_commit(nvs_handle);

    nvs_close(nvs_handle);

    ESP_LOGI(TAG_NVS, "NVS file counter updated");

    status_event_t evt = EVT_NVS_DONE;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
    vTaskDelete(NULL);
}

static esp_err_t lora_init(sx126x_handle_t *lora_handle) {
    esp_err_t err = ESP_OK;

    /* SX1262 LoRa struct setup */
    sx126x_config_t lora_cfg = {
        .spi_host          = SPI_HOST,
        .ss                = LORA_CS,
        .reset             = LORA_RESET,
        .busy              = LORA_BUSY,
        .txen              = -1,
        .rxen              = -1,
        .frequency         = LORA_FREQUENCY,
        .tx_power          = 22,
        .tcxo_voltage      = 0.0f,
        .use_regulator_ldo = false,
        .spreading_factor  = LORA_SPREADING_FACTOR,
        .bandwidth         = LORA_BANDWIDTH,
        .coding_rate       = LORA_CODING_RATE,
        .preamble_length   = 12,    // 8 -> SF7-8 || 12 -> SF5-6
        .payload_len       = 0,     // Variable length packet
        .crc_on            = true,  // true -> drop garbage
        .invert_iq         = false, // false -> normal communication
    };

    /* SX1262 LoRa initialization */
    err = LoRaInit(&lora_cfg, lora_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LORA, "LoRa init failed: %s", esp_err_to_name(err));
        return err;
    }

    LoRaDebugPrint(*lora_handle, false);
    err = LoRaBegin(*lora_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LORA, "LoRa init failed: %s", esp_err_to_name(err));
        return err;
    }

    LoRaConfig(*lora_handle);

    ESP_LOGI(TAG_LORA, "LoRa initialized");
    return ESP_OK;
}

void task_lora(void *pvParameters) {
    esp_err_t       err;
    sx126x_handle_t lora_handle;
    bool            ok;
    uint16_t        lost;

    err = lora_init(&lora_handle);
    if (err != ESP_OK) {
        goto setup_error;
    }

    uint16_t irqMask  = SX126X_IRQ_TX_DONE;
    uint16_t dio1Mask = SX126X_IRQ_TX_DONE;
    SetDioIrqParams(lora_handle, irqMask, dio1Mask, 0, 0);

    // ADICIONAR BATCH PACKET ---------------------------------------------------------------

    /* SX1262 DIO1 ISR initialization */
    err = gpio_isr_handler_add(LORA_DIO1, dio1_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LORA, "DIO1 ISR failed: %s", esp_err_to_name(err));
        goto setup_error;
    }

    /* LoRa initialized -> Wait for acquisition to start */
    xEventGroupSetBits(xInitEvent, LORA_INIT);
    xEventGroupWaitBits(xStatusEvent, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    // LORA RECEIVE
    // EVENT IGNIÇÃO

    xEventGroupWaitBits(xStatusEvent, SEND_DATA, pdFALSE, pdTRUE, portMAX_DELAY);

    /* Send ADS samples */
    for (uint32_t i = 0; i < sys_data_g.ads_sample; i++) {
        ClearIrqStatus(lora_handle, SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT);
        ok = LoRaSend(lora_handle, (uint8_t *)&ads_data_g[i], sizeof(ads_data_t), SX126x_TXMODE_ASYNC);
        if (!ok) {
            ESP_LOGI(TAG_LORA, "Sample %lu lost.", i);
            continue;
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LORA_DIO1_TIMEOUT_MS)) == 0) {
            ESP_LOGW(TAG_LORA, "TX notify timeout at sample %lu", i);
        }

        uint16_t irq = GetIrqStatus(lora_handle);
        if (irq != 0) {
            ClearIrqStatus(lora_handle, irq);
        }
    }
    lost = GetPacketLost(lora_handle);
    ESP_LOGI(TAG_LORA, "Samples lost: %u", lost);

    /* Send MAX samples */
    for (uint32_t i = 0; i < sys_data_g.max_sample; i++) {
        ClearIrqStatus(lora_handle, SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT);
        ok = LoRaSend(lora_handle, (uint8_t *)&max_data_g[i], sizeof(max_data_t), SX126x_TXMODE_ASYNC);
        if (!ok) {
            ESP_LOGI(TAG_LORA, "Sample %lu lost.", i);
            continue;
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LORA_DIO1_TIMEOUT_MS)) == 0) {
            ESP_LOGW(TAG_LORA, "TX notify timeout at sample %lu", i);
        }

        uint16_t irq = GetIrqStatus(lora_handle);
        if (irq != 0) {
            ClearIrqStatus(lora_handle, irq);
        }
    }

    lost = GetPacketLost(lora_handle);
    ESP_LOGI(TAG_LORA, "Samples lost: %u", lost);

    ESP_LOGI(TAG_LORA, "SEND_DATA complete");

    status_event_t evt = EVT_SEND_DONE;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);

cleanup:
    gpio_intr_disable(LORA_DIO1);
    gpio_isr_handler_remove(LORA_DIO1);

    vTaskDelete(NULL);

setup_error: {
    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
}

    goto cleanup;
}