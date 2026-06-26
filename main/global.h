#pragma once

#include "header.h"

/* DATA MANAGEMENT */
extern ads_data_t    *ads_data_g;
extern max_data_t    *max_data_g;
extern sys_data_t     sys_data_g;
extern file_counter_t file_counter_g;

/* QUEUE HANDLE*/
extern QueueHandle_t xEventQueue;
extern QueueHandle_t xTelemQueue;

/* EVENT HANDLE*/
extern EventGroupHandle_t xStatusEventGroup;
extern EventGroupHandle_t xInitEventGroup;

/* TASK HANDLE */
extern TaskHandle_t xTaskStatus;
extern TaskHandle_t xTaskIgnite;
extern TaskHandle_t xTaskAds;
extern TaskHandle_t xTaskLora;

/* TASKS */
void task_setup(void *pvParameters);
void task_status(void *pvParameters);
void task_arm(void *pvParameters);
void task_ignite(void *pvParameters);
void task_ads(void *pvParameters);
void task_max(void *pvParameters);
void task_sd(void *pvParameters);
void task_lfs(void *pvParameters);
void task_nvs(void *pvParameters);
void task_lora(void *pvParameters);
void task_log(void *pvParameters);

/* GLOBAL FUNCTIONS */
void telem_post(packet_type_t type, const char *msg);