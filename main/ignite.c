#include "global.h"

static const char *TAG_IGN = "ignition";

void task_ignite(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xStatusEvent, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    while (true) {
        ignition_event_t ign_evt;

        /* Wait for ignition */
        if (xQueueReceive(xIgnitionQueue, &ign_evt, portMAX_DELAY) != pdTRUE)
            continue;

        if (ign_evt == EVT_IGNITION_START) {
            bool ignited = false;

            for (int i = 0; i < 3; i++) {
                ESP_LOGW(TAG_IGN, "IGNITION STARTED");

                gpio_set_level(IGNITION_GPIO, HIGH);
                vTaskDelay(pdMS_TO_TICKS(1));
                gpio_set_level(IGNITION_GPIO, LOW);

                if (gpio_get_level(SQUIB_GPIO) == LOW) {
                    ignited = true;
                    break;
                }
            }

            if (ignited) {
                status_event_t evt = EVT_IGNITION_DONE;
                xQueueSend(xStatusQueue, &evt, portMAX_DELAY);

                ignition_event_t ign_evt = EVT_IGNITION_SUCCESS;
                xQueueSend(xIgnitionQueue, &ign_evt, portMAX_DELAY);

                break;
            } else {
                ign_evt = EVT_IGNITION_FAILED;
                xQueueSend(xIgnitionQueue, &ign_evt, portMAX_DELAY);
            }
        }
    }

    vTaskDelete(NULL);
}