#include "global.h"

static const char *TAG_IGN = "ignition";

void task_ignite(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xStatusEventGroup, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    while (true) {
        uint16_t wait_time = 10;

        /* Wait for ignition */
        while (wait_time > 0) {
            ESP_LOGI(TAG_IGN, "Countdown: %u", wait_time);
            vTaskDelay(pdMS_TO_TICKS(1000));
            wait_time--;
        }

        while (true) {
            bool ignited = false;

            for (int i = 0; i < 3; i++) {
                ESP_LOGW(TAG_IGN, "IGNITION PULSE %d/3", i + 1);

                gpio_set_level(IGNITION_GPIO, HIGH);
                vTaskDelay(pdMS_TO_TICKS(1)); // TESTAR ==============================================
                gpio_set_level(IGNITION_GPIO, LOW);

                vTaskDelay(pdMS_TO_TICKS(10));

                if (gpio_get_level(SQUIB_GPIO) == LOW) {
                    ignited = true;
                    break;
                }
            }

            if (ignited) {
                ESP_LOGW(TAG_IGN, "ACQUISITION STARTING");
                status_event_t evt = EVT_IGNITION_DONE;
                xQueueSend(xEventQueue, &evt, portMAX_DELAY);
                break;
            } else {
                ESP_LOGW(TAG_IGN, "Ignition not detected, retrying");
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }

        break;
    }

    vTaskDelete(NULL);
}