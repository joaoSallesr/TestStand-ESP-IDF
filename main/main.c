#include "global.h"

static const char *TAG_MAIN = "main";

void app_main(void) {
    ESP_LOGI(TAG_MAIN, "Starting main application");
    xTaskCreatePinnedToCore(task_setup, "Startup", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 1);
    xEventGroupSetBits(xStatusEvent, SETUP);

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

    vTaskDelay(pdMS_TO_TICKS(150)); // Wait for peripherals to stabilize

    // FORMAT MODE (NVS, SD, LFS) =====================================================================================

    // FORMAT MODE (NVS, SD, LFS) =====================================================================================

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
    xTaskCreatePinnedToCore(task_status, "Status", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(task_arm, "ARM", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_ignition, "Ignition", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_ads, "ADS", configMINIMAL_STACK_SIZE * 8, NULL, 8, &xTaskAds, 1);
    xTaskCreatePinnedToCore(task_max, "MAX", configMINIMAL_STACK_SIZE * 8, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_sd, "SD", configMINIMAL_STACK_SIZE * 8, NULL, 8, NULL, 1);
    xTaskCreatePinnedToCore(task_lfs, "LittleFS", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_nvs, "NVS", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(task_lora, "LoRa", configMINIMAL_STACK_SIZE * 8, NULL, 3, &xTaskLora, 1);
    // task log ?
}