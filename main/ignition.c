#include "global.h"

static const char *TAG_ARM = "ARM";
static const char *TAG_IGN = "ignition";

void task_arm(void *pvParameters) {

    /* Wait for lora_init */
    xEventGroupWaitBits(xSystemEvent, LORA_INIT, pdFALSE, pdTRUE, portMAX_DELAY);

    // TODO:
    // Armar sistema ao receber sinal ARMED da base

    while (true) {

        // true -> receber sinal ARMED
        if (true)
            break;
    }

    sys_event_t evt = EVT_ARM;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);
}

void task_ignition(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xSystemEvent, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    // TODO:
    // Estourar o Squib ao receber sinal IGNITION da base

    while (true) {

        // true -> receber sinal IGNITION
        if (true) {
            gpio_set_level(SQUIB_GPIO, HIGH);
            ESP_LOGW(TAG_IGN, "IGNITION STARTED");
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(SQUIB_GPIO, LOW);
            break;
        }
    }

    sys_event_t evt = EVT_IGNITION;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);
}