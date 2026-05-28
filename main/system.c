#include "global.h"

static const char *TAG_SYS = "SYS";

#define FORMAT_MODE false

static esp_err_t setup_memory(void) {
    ESP_LOGI(TAG_SYS,
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

    // sdkconfig -> + Support for external, SPI-connected RAM
    ads_data_g = (ads_data_t *)heap_caps_aligned_alloc(4, ADS_SAMPLES * sizeof(ads_data_t), MALLOC_CAP_SPIRAM);
    max_data_g = (max_data_t *)heap_caps_aligned_alloc(4, MAX_SAMPLES * sizeof(max_data_t), MALLOC_CAP_SPIRAM);

    if (ads_data_g == NULL) {
        ESP_LOGE(TAG_SYS, "Failed to allocate PSRAM for ADS data");
        // IMPLEMENTAR ERROR HANDLING -------------------------------

        status_event_t evt = EVT_FATAL_ERROR;
        xQueueSend(xEventQueue, &evt, portMAX_DELAY);
        return ESP_ERR_NO_MEM;
    }

    if (max_data_g == NULL) {
        ESP_LOGE(TAG_SYS, "Failed to allocate PSRAM for MAX data");
        // IMPLEMENTAR ERROR HANDLING -------------------------------

        status_event_t evt = EVT_FATAL_ERROR;
        xQueueSend(xEventQueue, &evt, portMAX_DELAY);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG_SYS,
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

    return ESP_OK;
}

static esp_err_t setup_peripherals(void) {
    esp_err_t err = ESP_OK;

    /* SPI bus configuration */
    spi_bus_config_t spi_bus_cfg = {
        .mosi_io_num     = MOSI,
        .miso_io_num     = MISO,
        .sclk_io_num     = CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4096,
    };

    /* SPI bus intialization */
    spi_host_device_t host     = SPI_HOST;
    spi_dma_chan_t    dma_chan = DMA_CHAN;

    err = spi_bus_initialize(host, &spi_bus_cfg, dma_chan);
    if (err != ESP_OK)
        return err;

    /* DRDY config with ISR */
    gpio_config_t drdy_conf = {
        .pin_bit_mask = (1ULL << LOADCELL_DRDY),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE // Trigger when DRDY goes LOW
    };

    /* DIO1 config with ISR */
    gpio_config_t dio1_conf = {
        .pin_bit_mask = (1ULL << LORA_DIO1),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE // Trigger when DIO1 goes HIGH
    };

    /* Apply ISR */
    ESP_ERROR_CHECK(gpio_config(&drdy_conf));
    ESP_ERROR_CHECK(gpio_config(&dio1_conf));
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    return err;
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

    uint32_t sd_num  = 0;
    uint32_t lfs_num = 0;

    nvs_get_u32(nvs_handle, "sd_counter", &sd_num);
    nvs_get_u32(nvs_handle, "lfs_counter", &lfs_num);

    if (format_mode) {
        sd_num  = 0;
        lfs_num = 0;
    }

    nvs_close(nvs_handle);

    file_counter_g.sd_files  = sd_num;
    file_counter_g.lfs_files = lfs_num;
    file_counter_g.format    = format_mode;
}

void task_setup(void *pvParameters) {
    esp_err_t err = ESP_OK;

    ESP_LOGE(TAG_SYS, "Allocating PSRAM");
    setup_memory();

    ESP_LOGE(TAG_SYS, "GPIO configuration");
    setup_peripherals();

    ESP_LOGE(TAG_SYS, "NVS flash init");
    setup_nvs(FORMAT_MODE);

    if (err == ESP_OK) {
        status_event_t evt = EVT_SETUP;
        xQueueSend(xEventQueue, &evt, portMAX_DELAY);

        vTaskDelete(NULL);
    }
}

void task_status(void *pvParameters) {
    status_event_t evt;

    while (true) {
        if (xQueueReceive(xEventQueue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        switch (evt) {

        case EVT_SETUP:
            ESP_LOGI(TAG_SYS, "SETUP -> IDLE");
            xEventGroupClearBits(xStatusEvent, SETUP);
            xEventGroupSetBits(xStatusEvent, IDLE);
            break;

        case EVT_ARM:
            ESP_LOGI(TAG_SYS, "IDLE -> ARMED");
            xEventGroupClearBits(xStatusEvent, IDLE);
            xEventGroupSetBits(xStatusEvent, ARMED);
            break;

        case EVT_IGNITION:
            ESP_LOGI(TAG_SYS, "ARMED -> FULL_ACQ");
            xEventGroupClearBits(xStatusEvent, ARMED);
            xEventGroupSetBits(xStatusEvent, FULL_ACQ);
            break;

        case EVT_ADS_DONE:
            ESP_LOGI(TAG_SYS, "FULL_ACQ -> PART_ACQ");
            xEventGroupClearBits(xStatusEvent, FULL_ACQ);
            xEventGroupSetBits(xStatusEvent, PART_ACQ);
            break;

        case EVT_MAX_DONE:
            ESP_LOGI(TAG_SYS, "PART_ACQ -> SAVE_DATA");
            xEventGroupClearBits(xStatusEvent, PART_ACQ);
            xEventGroupSetBits(xStatusEvent, SAVE_DATA);
            break;

        case EVT_SAVE_DONE:
            ESP_LOGI(TAG_SYS, "SAVE_DATA -> NVS_EDIT");
            xEventGroupClearBits(xStatusEvent, SAVE_DATA);
            xEventGroupClearBits(xStatusEvent, SD_DONE);
            xEventGroupClearBits(xStatusEvent, LFS_DONE);
            xEventGroupSetBits(xStatusEvent, NVS_EDIT);
            break;

        case EVT_NVS_DONE:
            ESP_LOGI(TAG_SYS, "NVS_EDIT -> SEND_DATA");
            xEventGroupClearBits(xStatusEvent, NVS_EDIT);
            xEventGroupSetBits(xStatusEvent, SEND_DATA);
            break;

        case EVT_SEND_DONE:
            ESP_LOGI(TAG_SYS, "SEND_DATA -> END_TEST");
            xEventGroupClearBits(xStatusEvent, SEND_DATA);
            xEventGroupSetBits(xStatusEvent, END_TEST);
            ESP_LOGI(TAG_SYS, "Test complete.");
            break;
        }
    }
}

void task_arm(void *pvParameters) {
    /* Wait for idle */
    xEventGroupWaitBits(xStatusEvent, IDLE, pdFALSE, pdTRUE, portMAX_DELAY);

    // TODO:
    // Armar sistema ao receber sinal ARMED da base

    while (true) {

        // true -> receber sinal ARMED
        if (true) {
            ESP_LOGW(TAG_SYS, "SYSTEM ARMED");
            break;
        }
    }

    status_event_t evt = EVT_ARM;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);
}

void task_log(void *pvParameters) {}