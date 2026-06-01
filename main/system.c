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

        return ESP_ERR_NO_MEM;
    }

    if (max_data_g == NULL) {
        ESP_LOGE(TAG_SYS, "Failed to allocate PSRAM for MAX data");

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
    if (err != ESP_OK) {
        ESP_LOGE(TAG_SYS, "Failed to initialize SPI bus");
        return err;
    }

    /* GPIO Initalization */
    gpio_reset_pin(BUZZER_GPIO);
    gpio_set_direction(BUZZER_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_direction(SQUIB_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SQUIB_GPIO, GPIO_PULLUP_ONLY);

    gpio_reset_pin(IGNITION_GPIO);
    gpio_set_direction(IGNITION_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(IGNITION_GPIO, LOW);

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
    err = gpio_config(&drdy_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_SYS, "Failed to apply DRDY config");
        return err;
    }

    err = gpio_config(&dio1_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_SYS, "Failed to apply DIO1 config");
        return err;
    }

    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_SYS, "Failed to start ISR");
        return err;
    }

    if (xEventQueue == NULL)
        return ESP_ERR_INVALID_ARG;
    if (xIgnitionQueue == NULL)
        return ESP_ERR_INVALID_ARG;
    if (xStatusEvent == NULL)
        return ESP_ERR_INVALID_ARG;

    return err;
}

static esp_err_t setup_nvs(bool format_mode) {
    esp_err_t err = nvs_flash_init();

    if (err != ESP_OK) {
        ESP_LOGE("NVS", "%s, erasing NVS partition...", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
        if (err != ESP_OK)
            return err;
    }

    nvs_handle_t nvs_handle;
    ESP_LOGI("NVS", "Opening Non-Volatile Storage (NVS) handle... ");
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

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

    return err;
}

void task_setup(void *pvParameters) {
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG_SYS, "Allocating PSRAM");
    err = setup_memory();
    if (err != ESP_OK) {
        status_event_t evt = EVT_SETUP_FAILED;
        xQueueSend(xEventQueue, &evt, portMAX_DELAY);

        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG_SYS, "GPIO configuration");
    err = setup_peripherals();
    if (err != ESP_OK) {
        status_event_t evt = EVT_SETUP_FAILED;
        xQueueSend(xEventQueue, &evt, portMAX_DELAY);

        vTaskDelete(NULL);
    }

    ESP_LOGI(TAG_SYS, "NVS flash init");
    err = setup_nvs(FORMAT_MODE);
    if (err != ESP_OK) {
        status_event_t evt = EVT_SETUP_FAILED;
        xQueueSend(xEventQueue, &evt, portMAX_DELAY);

        vTaskDelete(NULL);
    }

    if (err == ESP_OK) {
        status_event_t evt = EVT_SETUP_OK;
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

        case EVT_SETUP_OK:
            ESP_LOGI(TAG_SYS, "SETUP_START -> SETUP_OK");
            xEventGroupClearBits(xStatusEvent, SETUP_START);
            xEventGroupSetBits(xStatusEvent, SETUP_OK);
            break;

        case EVT_SETUP_FAILED:
            ESP_LOGI(TAG_SYS, "SETUP -> SETUP_FAIL");
            xEventGroupClearBits(xStatusEvent, SETUP_START);
            xEventGroupSetBits(xStatusEvent, SETUP_FAIL);
            break;

        case EVT_ARM:
            ESP_LOGI(TAG_SYS, "SETUP_OK -> ARMED");
            xEventGroupClearBits(xStatusEvent, SETUP_OK);
            xEventGroupSetBits(xStatusEvent, ARMED);
            break;

        case EVT_IGNITION_DONE:
            ESP_LOGI(TAG_SYS, "ARMED -> ACQUIRE");
            xEventGroupClearBits(xStatusEvent, ARMED);
            xEventGroupSetBits(xStatusEvent, ACQUIRE);
            break;

        case EVT_ADS_DONE: {
            ESP_LOGI(TAG_SYS, "ACQUIRE + ADS_DONE");
            EventBits_t bits = xEventGroupSetBits(xStatusEvent, ADS_DONE);

            if ((bits & (ADS_DONE | MAX_DONE)) == (ADS_DONE | MAX_DONE)) {
                status_event_t evt = EVT_ACQUIRE_DONE;
                xQueueSend(xEventQueue, &evt, portMAX_DELAY);
            }
            break;
        }

        case EVT_MAX_DONE: {
            ESP_LOGI(TAG_SYS, "ACQUIRE + MAX_DONE");
            EventBits_t bits = xEventGroupSetBits(xStatusEvent, MAX_DONE);

            if ((bits & (ADS_DONE | MAX_DONE)) == (ADS_DONE | MAX_DONE)) {
                status_event_t evt = EVT_ACQUIRE_DONE;
                xQueueSend(xEventQueue, &evt, portMAX_DELAY);
            }
            break;
        }

        case EVT_ACQUIRE_DONE:
            ESP_LOGI(TAG_SYS, "ACQUIRE -> SAVE_DATA");
            xEventGroupClearBits(xStatusEvent, ACQUIRE);
            xEventGroupClearBits(xStatusEvent, ADS_DONE);
            xEventGroupClearBits(xStatusEvent, MAX_DONE);
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
    EventBits_t bits = xEventGroupWaitBits(xStatusEvent, SETUP_OK | SETUP_FAIL, pdFALSE, pdFALSE, portMAX_DELAY);

    /* Check before Arming */
    if (bits & SETUP_OK) {
        ESP_LOGW(TAG_SYS, "SYSTEM ARMED");
        status_event_t evt = EVT_ARM;
        xQueueSend(xEventQueue, &evt, portMAX_DELAY);
    }

    if (bits & SETUP_FAIL) {
        ESP_LOGW(TAG_SYS, "SYSTEM ERROR - ABORTING TEST");
    }

    vTaskDelete(NULL);
}

void task_log(void *pvParameters) {}