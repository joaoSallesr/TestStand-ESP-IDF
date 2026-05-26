#include "global.h"

static const char *TAG_MAIN = "main";
static const char *TAG_SYS  = "system";

#define FORMAT_MODE      false
#define EVENT_QUEUE_SIZE 10

static void setup_memory(void) {
    // sdkconfig -> + Support for external, SPI-connected RAM
    ads_data_g = (ads_data_t *)heap_caps_aligned_alloc(4, ADS_SAMPLES * sizeof(ads_data_t), MALLOC_CAP_SPIRAM);
    max_data_g = (max_data_t *)heap_caps_aligned_alloc(4, MAX_SAMPLES * sizeof(max_data_t), MALLOC_CAP_SPIRAM);

    if (ads_data_g == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to allocate PSRAM for ADS data");
        // IMPLEMENTAR ERROR HANDLING -------------------------------
        return;
    }

    if (max_data_g == NULL) {
        ESP_LOGE(TAG_MAIN, "Failed to allocate PSRAM for MAX data");
        // IMPLEMENTAR ERROR HANDLING -------------------------------
        return;
    }
}

static void setup_peripherals(void) {
    // SPI bus configuration
    spi_bus_config_t spi_bus_cfg = {
        .mosi_io_num     = MOSI,
        .miso_io_num     = MISO,
        .sclk_io_num     = CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };

    // SPI host setup
    spi_host_device_t host = SPI_HOST;

    // SPI dma setup
    spi_dma_chan_t dma_chan = DMA_CHAN;

    // SPI bus initialization (2 ADS1256, 1 MAX31865 and 2 MAX6675)
    ESP_ERROR_CHECK(spi_bus_initialize(host, &spi_bus_cfg, dma_chan));

    // DRDY config with ISR
    gpio_config_t drdy_conf = {
        .pin_bit_mask = (1ULL << LOADCELL_DRDY),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE // Trigger when DRDY goes LOW
    };

    // DIO1 config with ISR
    gpio_config_t dio1_conf = {
        .pin_bit_mask = (1ULL << LORA_DIO1),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE // Trigger when DIO1 goes HIGH
    };

    // Apply config
    ESP_ERROR_CHECK(gpio_config(&drdy_conf));
    ESP_ERROR_CHECK(gpio_config(&dio1_conf));

    // Start ISR
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
}

static void setup_nvs(bool format_mode) {
    esp_err_t err = nvs_flash_init();

    if (err != ESP_OK) {
        ESP_LOGE("NVS", "%s, erasing NVS partition...", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    nvs_handle_t nvs_handle;
    ESP_LOGI("NVS", "Opening Non-Volatile Storage (NVS) handle... ");
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_handle));

    int32_t sd_num  = 0;
    int32_t lfs_num = 0;

    nvs_get_i32(nvs_handle, "sd_counter", &sd_num);
    nvs_get_i32(nvs_handle, "lfs_counter", &lfs_num);

    if (format_mode) {
        sd_num  = 0;
        lfs_num = 0;
    }

    nvs_close(nvs_handle);

    file_counter_g.sd_files  = sd_num;
    file_counter_g.lfs_files = lfs_num;
    file_counter_g.format    = format_mode;
}

void task_status(void *pvParameters) {
    sys_event_t evt;
    while (true) {
        if (xQueueReceive(xEventQueue, &evt, portMAX_DELAY) != pdTRUE)
            continue;
    }

    switch (evt) {

    case EVT_ARM:
        ESP_LOGI(TAG_SYS, "IDLE -> ARMED");
        xEventGroupClearBits(xSystemEvent, IDLE);
        xEventGroupSetBits(xSystemEvent, ARMED);
        break;

    case EVT_IGNITION:
        ESP_LOGI(TAG_SYS, "ARMED -> FULL_ACQ");
        xEventGroupClearBits(xSystemEvent, ARMED);
        xEventGroupSetBits(xSystemEvent, FULL_ACQ);
        break;

    case EVT_ADS_DONE:
        ESP_LOGI(TAG_SYS, "FULL_ACQ -> PART_ACQ");
        xEventGroupClearBits(xSystemEvent, FULL_ACQ);
        xEventGroupSetBits(xSystemEvent, PART_ACQ);
        break;

    case EVT_MAX_DONE:
        ESP_LOGI(TAG_SYS, "PART_ACQ -> SAVE_DATA");
        xEventGroupClearBits(xSystemEvent, PART_ACQ);
        xEventGroupSetBits(xSystemEvent, SAVE_DATA);
        break;

    case EVT_SAVE_DONE:
        ESP_LOGI(TAG_SYS, "SAVE_DATA -> NVS_EDIT");
        xEventGroupClearBits(xSystemEvent, SAVE_DATA);
        xEventGroupClearBits(xSystemEvent, SD_DONE);
        xEventGroupClearBits(xSystemEvent, LFS_DONE);
        xEventGroupSetBits(xSystemEvent, NVS_EDIT);
        break;

    case EVT_NVS_DONE:
        ESP_LOGI(TAG_SYS, "NVS_EDIT -> SEND_DATA");
        xEventGroupClearBits(xSystemEvent, NVS_EDIT);
        xEventGroupSetBits(xSystemEvent, SEND_DATA);
        break;

    case EVT_SEND_DONE:
        ESP_LOGI(TAG_SYS, "SEND_DATA -> END_TEST");
        xEventGroupClearBits(xSystemEvent, SEND_DATA);
        xEventGroupSetBits(xSystemEvent, END_TEST);
        ESP_LOGI(TAG_SYS, "Test complete.");
        break;
    }
}

void app_main(void) {
    ESP_LOGI(TAG_MAIN, "Starting main application");

    // TESTAR
    ESP_LOGI(TAG_MAIN,
             "[ BEFORE ] - Free Heap: %u bytes\n"
             "  MALLOC_CAP_8BIT      %7zu bytes\n"
             "  MALLOC_CAP_DMA       %7zu bytes\n"
             "  MALLOC_CAP_SPIRAM    %7zu bytes\n"
             "  MALLOC_CAP_INTERNAL  %7zu bytes\n"
             "  MALLOC_CAP_DEFAULT   %7zu bytes\n"
             "  MALLOC_CAP_IRAM_8BIT %7zu bytes\n"
             "  MALLOC_CAP_RETENTION %7zu bytes\n",
             xPortGetFreeHeapSize(), heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_DEFAULT), heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT),
             heap_caps_get_free_size(MALLOC_CAP_RETENTION));
    // TESTAR

    setup_memory();
    setup_peripherals();
    setup_nvs(FORMAT_MODE);
    vTaskDelay(pdMS_TO_TICKS(150)); // Wait for peripherals to stabilize

    // FORMAT MODE (NVS, SD, LFS) =====================================================================================

    // FORMAT MODE (NVS, SD, LFS) =====================================================================================

    /* Create Queue */
    xEventQueue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(sys_event_t));

    /* Create Mutexes */
    // xDATAMutex = xSemaphoreCreateMutex();

    // TESTAR
    ESP_LOGI(TAG_MAIN,
             "[ AFTER ] - Free Heap: %u bytes\n"
             "  MALLOC_CAP_8BIT      %7zu bytes\n"
             "  MALLOC_CAP_DMA       %7zu bytes\n"
             "  MALLOC_CAP_SPIRAM    %7zu bytes\n"
             "  MALLOC_CAP_INTERNAL  %7zu bytes\n"
             "  MALLOC_CAP_DEFAULT   %7zu bytes\n"
             "  MALLOC_CAP_IRAM_8BIT %7zu bytes\n"
             "  MALLOC_CAP_RETENTION %7zu bytes\n",
             xPortGetFreeHeapSize(), heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_DEFAULT), heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT),
             heap_caps_get_free_size(MALLOC_CAP_RETENTION));
    // TESTAR

    /* Create Tasks */
    // Verificar parametros de criação das task
    // task log ?
    xTaskCreatePinnedToCore(task_status, "Status", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(task_ads, "ADS", configMINIMAL_STACK_SIZE * 8, NULL, 10, &xTaskAds, 1);
    xTaskCreatePinnedToCore(task_max, "MAX", configMINIMAL_STACK_SIZE * 8, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_sd, "SD", configMINIMAL_STACK_SIZE * 8, NULL, 3, NULL, 1);
    // task littlefs
    xTaskCreatePinnedToCore(task_lora, "LoRa", configMINIMAL_STACK_SIZE * 8, NULL, 3, &xTaskLora, 1);

    xEventGroupSetBits(xSystemEvent, IDLE);
}