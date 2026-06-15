#include "global.h"

static const char *TAG_MAIN = "main";

#define EVENT_QUEUE_SIZE    10
#define IGNITION_QUEUE_SIZE 1

void app_main(void) {
    ESP_LOGI(TAG_MAIN, "Starting main application");

    /* Create Queue */
    xStatusQueue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(status_event_t));

    /* Create Event Group */
    xStatusEvent = xEventGroupCreate();
    xInitEvent   = xEventGroupCreate();

    /* Setup Tasks */
    xTaskCreatePinnedToCore(task_setup, "Setup", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_status, "Status", configMINIMAL_STACK_SIZE * 8, NULL, 10, &xTaskStatus, 0);
    xEventGroupWaitBits(xStatusEvent, TASK_INIT, pdFALSE, pdTRUE, portMAX_DELAY);

    /* Peripherals Tasks */
    // Verificar parametros de criação das task
    // ==========================================================================
    xTaskCreatePinnedToCore(task_arm, "ARM", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_ignite, "Ignition", configMINIMAL_STACK_SIZE * 8, NULL, 10, &xTaskIgnite, 0);
    xTaskCreatePinnedToCore(task_ads, "ADS", configMINIMAL_STACK_SIZE * 8, NULL, 8, &xTaskAds, 1);
    xTaskCreatePinnedToCore(task_max, "MAX", configMINIMAL_STACK_SIZE * 8, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_sd, "SD", configMINIMAL_STACK_SIZE * 8, NULL, 8, NULL, 1);
    xTaskCreatePinnedToCore(task_lfs, "LittleFS", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_nvs, "NVS", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(task_lora, "LoRa", configMINIMAL_STACK_SIZE * 8, NULL, 3, &xTaskLora, 1);
    // task log ?

    // FORMAT MODE (NVS, SD, LFS) =====================================================================================

    // FORMAT MODE (NVS, SD, LFS) =====================================================================================
}