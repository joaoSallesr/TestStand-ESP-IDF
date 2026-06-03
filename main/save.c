#include "global.h"

static const char *TAG_SD  = "SD";
static const char *TAG_LFS = "LittleFS";
static const char *TAG_NVS = "NVS";

/* SD & LITTLEFS CONFIG */
#define SD_MAX_FILES    5
#define SD_MOUNT        "/sdcard"
#define SD_BUFFER_SIZE  32 * 1024
#define SD_UNIT_SIZE    32 * 1024
#define LFS_MAX_FILES   32
#define LFS_BUFFER_SIZE 32 * 1024
#define LFS_MAX_FLASH   0.9 // Maximum percentage of flash to be used by littlefs
#define FILENAME_LENGTH 32

void task_sd(void *pvParameters) {
    esp_err_t     err;
    sdmmc_card_t *card;
    uint8_t      *sd_dma_buf = NULL;

    bool sd_mounted = false;

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
        ESP_LOGE(TAG_SD, "Failed to mount SD card");
        goto setup_error;
    }

    ESP_LOGI(TAG_SD, "Filesystem mounted");
    sdmmc_card_print_info(stdout, card);
    sd_mounted = true;

    /* SD format mode */
    if (file_counter_g.format == true) {
        ESP_LOGW(TAG_SD, "Format mode enabled, formatting SD card");
        err = esp_vfs_fat_sdcard_format(SD_MOUNT, card);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_SD, "Failed to format SD card: %s", esp_err_to_name(err));
            goto setup_error;
        }
        goto format_device;
    }

    /* Allocate DMA-capable internal buffer */
    sd_dma_buf = heap_caps_malloc(SD_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (sd_dma_buf == NULL) {
        ESP_LOGE(TAG_SD, "Failed to allocate DMA buffer");
        err = ESP_ERR_NO_MEM;
        goto setup_error;
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
    file_header_t sd_header = {
        .name_check  = 0xABCD1234,
        .ads_samples = ads_total,
        .max_samples = max_total,
        .timestamp   = (uint32_t)esp_timer_get_time(),
    };

    if (fwrite(&sd_header, sizeof(file_header_t), 1, f) != 1) {
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

            memcpy(sd_dma_buf, &ads_data_g[written], bytes);

            if (fwrite(sd_dma_buf, sample_size, batch, f) != batch) {
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

            memcpy(sd_dma_buf, &max_data_g[written], bytes);

            if (fwrite(sd_dma_buf, sample_size, batch, f) != batch) {
                ESP_LOGE(TAG_SD, "MAX write error at sample %lu", written);
                goto close;
            }
            written += batch;
        }

        ESP_LOGI(TAG_SD, "MAX: %lu samples written (%lu bytes)", written, written * sample_size);
    }

    ESP_LOGI(TAG_SD, "SD card data saving completed");

close:
    fclose(f);
    vTaskDelay(pdMS_TO_TICKS(20));

cleanup:
    free(sd_dma_buf);

    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    ESP_LOGI(TAG_SD, "Card unmounted");

    EventBits_t bits = xEventGroupSetBits(xStatusEvent, SD_DONE);
    if ((bits & (SD_DONE | LFS_DONE)) == (SD_DONE | LFS_DONE)) {
        status_event_t evt = EVT_SAVE_DONE;
        xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
    }

    vTaskDelete(NULL);

setup_error:
    ESP_LOGE(TAG_SD, "SD init failed: %s", esp_err_to_name(err));

    if (sd_mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
        ESP_LOGI(TAG_SD, "Card unmounted");
    }

    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);

format_device:
    ESP_LOGW(TAG_SD, "SD card formatted");

    esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
    ESP_LOGI(TAG_SD, "Card unmounted");

    vTaskDelete(NULL);
}

void task_lfs(void *pvParameters) {
    esp_err_t err;
    uint8_t  *lfs_dma_buf = NULL;

    bool lfs_mounted = false;

    /* Settings for initializing LittleFS */
    esp_vfs_littlefs_conf_t littlefs_cfg = {
        .base_path              = "/littlefs",
        .partition_label        = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount             = false,
    };

    /* LittleFS initialization */
    ESP_LOGI(TAG_LFS, "Initializing LittleFS");
    err = esp_vfs_littlefs_register(&littlefs_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LFS, "Failed to mount LittleFS");
        goto setup_error;
    }

    size_t lfs_size = 0;
    size_t lfs_used = 0;
    ESP_LOGI(TAG_LFS, "Filesystem mounted");
    err = esp_littlefs_info(littlefs_cfg.partition_label, &lfs_size, &lfs_used);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LFS, "Failed to get LittleFS partition information");
        goto setup_error;
    }
    lfs_mounted = true;

    /* LittleFS format mode */
    if (file_counter_g.format == true) {
        ESP_LOGW(TAG_LFS, "Format mode enabled, formatting LittleFS");
        err = esp_littlefs_format(littlefs_cfg.partition_label);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_LFS, "Failed to format LittleFS: %s", esp_err_to_name(err));
            goto setup_error;
        }
        goto format_device;
    }

    /* Allocate DMA-capable internal buffer */
    lfs_dma_buf = heap_caps_malloc(LFS_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (lfs_dma_buf == NULL) {
        ESP_LOGE(TAG_LFS, "Failed to allocate DMA buffer");
        err = ESP_ERR_NO_MEM;
        goto setup_error;
    }

    /* LittleFS initialized -> Wait for SAVE_DATA */
    xEventGroupSetBits(xInitEvent, LFS_INIT);
    xEventGroupWaitBits(xStatusEvent, SAVE_DATA, pdFALSE, pdTRUE, portMAX_DELAY);

    uint32_t ads_total = sys_data_g.ads_sample;
    uint32_t max_total = sys_data_g.max_sample;
    ESP_LOGI(TAG_LFS, "Saving %lu ADS samples, %lu MAX samples", ads_total, max_total);

    /* Create log file */
    char log_name[FILENAME_LENGTH];
    snprintf(log_name, FILENAME_LENGTH, "%s/flight%ld.bin", littlefs_cfg.base_path, file_counter_g.lfs_files);
    ESP_LOGI(TAG_LFS, "Created file %s", log_name);

    FILE *f = fopen(log_name, "wb");
    if (!f) {
        ESP_LOGE(TAG_LFS, "Failed to open file for writing");
        goto cleanup;
    }

    /* Write header */
    file_header_t lfs_header = {
        .name_check  = 0xABCD5678,
        .ads_samples = ads_total,
        .max_samples = max_total,
        .timestamp   = (uint32_t)esp_timer_get_time(),
    };

    if (fwrite(&lfs_header, sizeof(file_header_t), 1, f) != 1) {
        ESP_LOGE(TAG_LFS, "Failed to write header");
        goto close;
    }

    /* Write ADS data */
    {
        const size_t   sample_size = sizeof(ads_data_t);
        const uint32_t chunk       = LFS_BUFFER_SIZE / sample_size;
        uint32_t       written     = 0;

        while (written < ads_total) {
            uint32_t batch = ((ads_total - written) < chunk ? (ads_total - written) : chunk); // sets chunk size
            size_t   bytes = batch * sample_size;

            memcpy(lfs_dma_buf, &ads_data_g[written], bytes);

            if (fwrite(lfs_dma_buf, sample_size, batch, f) != batch) {
                ESP_LOGE(TAG_LFS, "ADS write error at sample %lu", written);
                goto close;
            }

            written += batch;
        }

        ESP_LOGI(TAG_LFS, "ADS: %lu samples written (%lu bytes)", written, written * sample_size);
    }

    /* Write MAX data */
    {
        const size_t   sample_size = sizeof(max_data_t);
        const uint32_t chunk       = LFS_BUFFER_SIZE / sample_size;
        uint32_t       written     = 0;

        while (written < max_total) {
            uint32_t batch = ((max_total - written) < chunk ? (max_total - written) : chunk); // sets chunk size
            size_t   bytes = batch * sample_size;

            memcpy(lfs_dma_buf, &max_data_g[written], bytes);

            if (fwrite(lfs_dma_buf, sample_size, batch, f) != batch) {
                ESP_LOGE(TAG_LFS, "MAX write error at sample %lu", written);
                goto close;
            }
            written += batch;
        }

        ESP_LOGI(TAG_LFS, "MAX: %lu samples written (%lu bytes)", written, written * sample_size);
    }

    ESP_LOGI(TAG_LFS, "LFS data saving completed");

close:
    fclose(f);
    vTaskDelay(pdMS_TO_TICKS(20));

cleanup:
    free(lfs_dma_buf);

    esp_vfs_littlefs_unregister(littlefs_cfg.partition_label);
    ESP_LOGI(TAG_LFS, "LittleFS unmounted");

    EventBits_t bits = xEventGroupSetBits(xStatusEvent, LFS_DONE);
    if ((bits & (SD_DONE | LFS_DONE)) == (SD_DONE | LFS_DONE)) {
        status_event_t evt = EVT_SAVE_DONE;
        xQueueSend(xStatusQueue, &evt, portMAX_DELAY);
    }

    vTaskDelete(NULL);

setup_error:
    ESP_LOGE(TAG_LFS, "LFS init failed: %s", esp_err_to_name(err));

    if (lfs_mounted) {
        esp_vfs_littlefs_unregister(littlefs_cfg.partition_label);
        ESP_LOGI(TAG_LFS, "LittleFS unmounted");
    }

    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);

format_device:
    ESP_LOGW(TAG_LFS, "LittleFS formatted");
    vTaskDelete(NULL);
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