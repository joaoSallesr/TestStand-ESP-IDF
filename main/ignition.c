#include "global.h"

static const char *TAG_IGN = "ignition";

void task_ignition(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xStatusEvent, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    // TODO:
    // Estourar o Squib ao receber sinal IGNITION da base
    // Ao receber o sinal envia EVT_IGNITION_START

    while (true) {
        status_event_t evt;

        if (xQueueReceive(xIgnitionQueue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        // true -> receber sinal IGNITION
        if (evt == EVT_IGNITION_START) {
            gpio_set_level(COMANDO_IGNITOR, HIGH);
            ESP_LOGW(TAG_IGN, "IGNITION STARTED");
            vTaskDelay(pdMS_TO_TICKS(10)); // ?????????
            gpio_set_level(COMANDO_IGNITOR, LOW);
            break;
        }
    }

    // Wait for squib reading
    while (gpio_get_level(LEITURA_SQUIB) == HIGH) {
        vTaskDelay(pdTICKS_TO_MS(1));
    }

    status_event_t evt = EVT_IGNITION_DONE;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);
}