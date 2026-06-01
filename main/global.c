#include "global.h"

/* DATA MANAGEMENT */
ads_data_t    *ads_data_g     = NULL;
max_data_t    *max_data_g     = NULL;
sys_data_t     sys_data_g     = {0};
file_counter_t file_counter_g = {0};

/* QUEUE HANDLE*/
QueueHandle_t xEventQueue    = NULL;
QueueHandle_t xIgnitionQueue = NULL;

/* EVENT HANDLE*/
EventGroupHandle_t xStatusEvent = NULL;

/* TASK HANDLE */
TaskHandle_t xTaskAds  = NULL;
TaskHandle_t xTaskLora = NULL;
