#pragma once

#include "header.h"

// DATA MANAGEMENT
extern ads_data_t    *ads_data_g;
extern max_data_t    *max_data_g;
extern sys_data_t     sys_data_g;
extern file_counter_t file_counter_g;

/* MUTEX */
// extern SemaphoreHandle_t xDATAMutex;
extern portMUX_TYPE xDATASpinlock;

/* QUEUE */
extern QueueHandle_t xEventQueue;

/* EVENT HANDLE*/
// Event group for NVS counter synchronization
extern EventGroupHandle_t xNVSCounterEvent;
// Event group for LittleFS and SD format synchronization
extern EventGroupHandle_t xFormatEvent;
// Event group for Task synchronization
extern EventGroupHandle_t xSystemEvent;

// TASK HANDLE
extern TaskHandle_t xTaskAds;
extern TaskHandle_t xTaskLora;

// TASKS
void task_status(void *pvParameters);
void task_ads(void *pvParameters);
void task_max(void *pvParameters);
void task_sd(void *pvParameters);
void task_littlefs(void *pvParameters);
void task_nvs(void *pvParameters);
void task_lora(void *pvParameters);
void task_log(void *pvParameters);