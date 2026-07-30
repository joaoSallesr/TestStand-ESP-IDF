#include "global.h"

static const char *TAG_MAIN = "main";

#define EVENT_QUEUE_SIZE 10
#define TELEM_QUEUE_SIZE 16

void app_main(void) {
    ESP_LOGI(TAG_MAIN, "Starting main application");

    /* Create Queue */
    xEventQueue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(status_event_t));
    xTelemQueue = xQueueCreate(TELEM_QUEUE_SIZE, sizeof(msg_packet_t));

    /* Create mutex */
    xSPIMutex = xSemaphoreCreateMutex();

    /* Create Event Group */
    xStatusEventGroup = xEventGroupCreate();
    xInitEventGroup   = xEventGroupCreate();

    /* Setup Tasks */
    xTaskCreatePinnedToCore(task_setup, "Setup", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_status, "Status", configMINIMAL_STACK_SIZE * 8, NULL, 10, &xTaskStatus, 0);
    xEventGroupWaitBits(xStatusEventGroup, TASK_INIT, pdFALSE, pdTRUE, portMAX_DELAY);

    /* Peripherals Tasks */
    xTaskCreatePinnedToCore(task_arm, "ARM", configMINIMAL_STACK_SIZE * 8, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(task_ignite, "Ignition", configMINIMAL_STACK_SIZE * 8, NULL, 10, &xTaskIgnite, 0);
    xTaskCreatePinnedToCore(task_ads, "ADS", configMINIMAL_STACK_SIZE * 8, NULL, 8, &xTaskAds, 1);
    xTaskCreatePinnedToCore(task_max, "MAX", configMINIMAL_STACK_SIZE * 8, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(task_sd, "SD", configMINIMAL_STACK_SIZE * 8, NULL, 8, NULL, 1);
    xTaskCreatePinnedToCore(task_lfs, "LittleFS", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(task_nvs, "NVS", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL, 1);
    // xTaskCreatePinnedToCore(task_lora, "LoRa", configMINIMAL_STACK_SIZE * 8, NULL, 3, &xTaskLora, 1);

    // ==========================================================================
    // task log ?
    // Verificar parametros de criação das task
}