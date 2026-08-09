#include "global.h"

static const char *TAG_IGN = "ignition";

void task_ignite(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xStatusEventGroup, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    while (true) {
        uint32_t cmd;

        /* Wait for ignition */
        xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, portMAX_DELAY);

        if (cmd == EVT_IGNITION_START) {
            bool ignited = false;

            for (int i = 0; i < 3; i++) {
                ESP_LOGW(TAG_IGN, "IGNITION STARTED");

                gpio_set_level(IGNITION_GPIO, HIGH);
                vTaskDelay(pdMS_TO_TICKS(1)); // TESTAR ==============================================
                gpio_set_level(IGNITION_GPIO, LOW);

                if (gpio_get_level(SQUIB_GPIO) == LOW) {
                    ignited = true;
                    break;
                }
            }

            if (ignited) {
                xTaskNotify(xTaskLora, EVT_IGNITION_SUCCESS, eSetValueWithOverwrite);
                break;
            } else {
                xTaskNotify(xTaskLora, EVT_IGNITION_FAILED, eSetValueWithOverwrite);
            }
        }
    }

    vTaskDelete(NULL);
}