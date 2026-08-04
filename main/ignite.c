#include "global.h"

static const char *TAG_IGN = "ignition";

static const bool ignore_btn = true;

void task_ignite(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xStatusEventGroup, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);
    if (ignore_btn) {
        for (int i = 0; i < 3; i++) {
            uint16_t wait_time = 10;
            bool     ignited   = false;
            /* Wait for ignition */
            while (wait_time > 0) {
                ESP_LOGI(TAG_IGN, "Countdown %d: %u", i, wait_time);
                vTaskDelay(pdMS_TO_TICKS(1000));
                wait_time--;
            }
            for (int j = 0; j < 3; j++) {
                ESP_LOGW(TAG_IGN, "IGNITION PULSE %d/3", j + 1);
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
            }
        }
    } else {
        /* Wait for operator to press the ignition button (active-low) */
        ESP_LOGI(TAG_IGN, "ARMED — press ignition button to fire");
        uint32_t log_ticks = 0;
        while (gpio_get_level(IGNITE_BTN) != LOW) {
            vTaskDelay(pdMS_TO_TICKS(100));
            log_ticks++;
            if (log_ticks % 50 == 0) // log every 5 s
                ESP_LOGI(TAG_IGN, "Waiting for ignition button...");
        }
        ESP_LOGW(TAG_IGN, "Ignition button pressed");

        bool ignited = false;

        for (int j = 0; j < 3; j++) {
            ESP_LOGW(TAG_IGN, "IGNITION PULSE %d/3", j + 1);

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
        } else {
            ESP_LOGE(TAG_IGN, "Ignition not detected after 3 pulses");
        }
    }

    vTaskDelete(NULL);
}