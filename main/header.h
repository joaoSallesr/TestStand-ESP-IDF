#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <esp_check.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_vfs_fat.h>
#include <hal/spi_types.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdmmc_cmd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

#include "esp_ads1256.h"
#include "max6675.h"
#include "ra01s.h"

#define LOW  0
#define HIGH 1

/* GPIO */
#define BUZZER_GPIO   GPIO_NUM_4
#define IGNITOR_GPIO  GPIO_NUM_5
#define SQUIB_GPIO    GPIO_NUM_7
#define MOSI          GPIO_NUM_11
#define MISO          GPIO_NUM_13
#define CLK           GPIO_NUM_12
#define SD_DAT0       GPIO_NUM_37
#define SD_DAT1       GPIO_NUM_35
#define SD_DAT2       GPIO_NUM_39
#define SD_DAT3       GPIO_NUM_48
#define SD_CLK        GPIO_NUM_36
#define SD_CMD        GPIO_NUM_38
#define LOADCELL_CS   GPIO_NUM_8
#define LOADCELL_DRDY GPIO_NUM_21
#define LOADCELL_SYNC GPIO_NUM_9
#define TRANS_CS      GPIO_NUM_42
#define TRANS_DRDY    GPIO_NUM_10
#define TRANS_SYNC    GPIO_NUM_47
#define MAX1_CS       GPIO_NUM_16
#define MAX2_CS       GPIO_NUM_17
#define MAX3_CS       GPIO_NUM_18
#define MAX3_DRDY     GPIO_NUM_6
#define LORA_CS       GPIO_NUM_14
#define LORA_DIO1     GPIO_NUM_2 // LORA_DRDY
#define LORA_BUSY     GPIO_NUM_41
#define LORA_RESET    GPIO_NUM_40

/* SPI CONFIG */
#define SPI_HOST SPI2_HOST
#define DMA_CHAN SPI_DMA_CH_AUTO

/* MEMORY CONFIG */
#define ADS_SAMPLES          7000
#define FULL_ACQ_DURATION_MS 7000

#define MAX_SAMPLES             700
#define PARTIAL_ACQ_DURATION_MS 20000

/* STATUS FLAGS */
#define SETUP     BIT(0)
#define IDLE      BIT(1)
#define ARMED     BIT(2)
#define FULL_ACQ  BIT(3)
#define PART_ACQ  BIT(4)
#define SD_DONE   BIT(5)
#define LFS_DONE  BIT(6)
#define SAVE_DATA BIT(7)
#define NVS_EDIT  BIT(8)
#define SEND_DATA BIT(9)
#define END_TEST  BIT(10)

/* SAMPLE STRUCTURES */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;    // 4 Bytes
    int32_t  thrust_raw;   // 4 Bytes
    int32_t  pressure_raw; // 4 Bytes
} ads_data_t;              // 12 Bytes/sample -> 12 * ADS_SAMPLES

typedef struct __attribute__((packed)) {
    uint32_t timestamp;        // 4 Bytes
    int16_t  temperature1_raw; // 2 Bytes
    int16_t  temperature2_raw; // 2 Bytes
} max_data_t;                  // 8 Bytes

/* SYSTEM STRUCTURES */
typedef struct {
    uint32_t ads_sample; // 4 Bytes
    uint32_t max_sample; // 4 Bytes
} sys_data_t;            // 8 Bytes

typedef struct __attribute__((packed)) {
    uint32_t name_check;  // 4 Bytes
    uint32_t ads_samples; // 4 Bytes
    uint32_t max_samples; // 4 Bytes
    uint32_t timestamp;   // 4 Byte
} file_header_t;          // 16 bytes

typedef struct __attribute__((packed)) {
    uint32_t sd_files;
    uint32_t lfs_files;
    bool     format;
} file_counter_t;

typedef enum {
    EVT_SETUP,     // setup finished
    EVT_ARM,       // system armed
    EVT_IGNITION,  // ignition started
    EVT_ADS_DONE,  // task_ads finished full acquisition
    EVT_MAX_DONE,  // task_max finished
    EVT_SAVE_DONE, // sd and lfs finished writing
    EVT_SEND_DONE, // task_lora finished sending
    EVT_NVS_DONE,  // task_nvs finished
} status_event_t;

typedef enum {
    EVT_FATAL_ERROR, // fatal error detected -> stop ignition
} system_event_t;