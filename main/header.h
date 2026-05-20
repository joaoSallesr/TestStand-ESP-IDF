#pragma once

// INCLUDES
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
#include "ra01s.h"

// GPIO
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
#define LORA_DIO1     GPIO_NUM_15 // LORA_DRDY
#define LORA_BUSY     GPIO_NUM_41
#define LORA_RESET    GPIO_NUM_40

// SPI CONFIG
#define SPI_HOST SPI2_HOST
#define DMA_CHAN SPI_DMA_CH_AUTO

// MEMORY CONFIG
#define MAX_SAMPLES 700
#define ADS_SAMPLES 7000

// STATUS FLAGS
#define IDLE      (1 << 0)
#define ARMED     (1 << 1)
#define FULL_ACQ  (1 << 2)
#define PART_ACQ  (1 << 3)
#define SD_DONE   (1 << 4)
#define LFS_DONE  (1 << 5)
#define SAVE_DATA (1 << 6)
#define NVS_EDIT  (1 << 7)
#define SEND_DATA (1 << 8)
#define END_TEST  (1 << 9)

// SAMPLE STRUCTURES
typedef struct __attribute__((packed)) {
    uint32_t timestamp; // 4 Bytes
    int32_t  loadcell;  // 4 Bytes
    int32_t  trans;     // 4 Bytes
} ads_data_t;           // 12 Bytes/sample -> 12 * ADS_SAMPLES

typedef struct __attribute__((packed)) {
    uint32_t timestamp; // 4 Bytes
    int16_t  max1;      // 2 Bytes
    int16_t  max2;      // 2 Bytes
    int16_t  max3;      // 2 Bytes
} max_data_t;           // 10 Bytes

// SYSTEM STRUCTURES
typedef struct __attribute__((packed)) {
    volatile uint32_t ads_sample; // 4 Bytes
    volatile uint32_t max_sample; // 4 Bytes
} sys_data_t;                     // 8 Bytes

typedef struct __attribute__((packed)) {
    uint32_t name_check;  // 4 Bytes
    uint32_t ads_samples; // 4 Bytes
    uint32_t max_samples; // 4 Bytes
    uint32_t timestamp;   // 4 Byte
} file_header_t;          // 16 bytes

typedef struct __attribute__((packed)) {
    uint32_t file_numSD;
    uint32_t file_numLFS;
    bool     format;
} file_counter_t;

typedef enum {
    EVT_ARM,       // system armed
    EVT_IGNITION,  // ignition started
    EVT_ADS_DONE,  // task_ads finished full acquisition
    EVT_MAX_DONE,  // task_max finished
    EVT_SAVE_DONE, // sd and lfs finished writing
    EVT_SEND_DONE, // task_lora finished sending
    EVT_NVS_DONE,  // task_nvs finished
} sys_event_t;