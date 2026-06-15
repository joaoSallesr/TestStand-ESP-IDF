#pragma once

#include "header.h"

/* DATA MANAGEMENT */
extern ads_data_t    *ads_data_g;
extern max_data_t    *max_data_g;
extern sys_data_t     sys_data_g;
extern file_counter_t file_counter_g;

/* QUEUE HANDLE*/
extern QueueHandle_t xStatusQueue;

/* EVENT HANDLE*/
extern EventGroupHandle_t xStatusEvent;
extern EventGroupHandle_t xInitEvent;

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