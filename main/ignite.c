#include "global.h"

static const char *TAG_IGN = "ignition";

void task_ignite(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xStatusEvent, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    // TODO:
    // Estourar o Squib ao receber sinal IGNITION da base
    // Ao receber o sinal envia EVT_IGNITION_START

    while (true) {
        ignition_event_t evt;

        /* Wait for ignition */
        if (xQueueReceive(xIgnitionQueue, &evt, portMAX_DELAY) != pdTRUE)
            continue;

        if (evt == EVT_IGNITION_START) {
            for (int i = 0; i < 3; i++) {
                ESP_LOGW(TAG_IGN, "IGNITION STARTED");

                gpio_set_level(IGNITION_GPIO, HIGH);
                vTaskDelay(pdMS_TO_TICKS(1)); // 1 ?
                gpio_set_level(IGNITION_GPIO, LOW);

                if (gpio_get_level(SQUIB_GPIO) == LOW)
                    break;
            }

            if (gpio_get_level(SQUIB_GPIO) == LOW)
                break;
        }
    }

    status_event_t evt = EVT_IGNITION_DONE;
    xQueueSend(xStatusQueue, &evt, portMAX_DELAY);

    vTaskDelete(NULL);
}