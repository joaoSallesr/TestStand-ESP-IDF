#include "global.h"

static const char *TAG_ARM = "ARM";
static const char *TAG_IGN = "ignition";

void task_arm(void *pvParameters) {

    /* Wait for lora_init */
    xEventGroupWaitBits(xSystemEvent, LORA_INIT, pdFALSE, pdTRUE, portMAX_DELAY);

    sys_event_t evt = EVT_ARM;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);
}

void task_ignition(void *pvParameters) {

    /* Wait for armed */
    xEventGroupWaitBits(xSystemEvent, ARMED, pdFALSE, pdTRUE, portMAX_DELAY);

    sys_event_t evt = EVT_IGNITION;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);
}