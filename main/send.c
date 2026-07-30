#include "global.h"

static const char *TAG_LORA = "LoRa";

/* LORA CONFIG */
#define LORA_FREQUENCY        915000000 // Hz
#define LORA_SPREADING_FACTOR 5
#define LORA_BANDWIDTH        SX126X_LORA_BW_500_0
#define LORA_CODING_RATE      SX126X_LORA_CR_4_5
#define LORA_DIO1_TIMEOUT_MS  1000
#define LORA_TELEM_MS         50

static void IRAM_ATTR dio1_isr_handler(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xTaskLora, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

void LoRaError(int error) {
    ESP_LOGE(TAG_LORA, "Fatal LoRa error: %d", error);
    gpio_intr_disable(LORA_DIO1);
    gpio_isr_handler_remove(LORA_DIO1);
    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);
    vTaskDelete(NULL);
}

/* Global packet creation */
void telem_post(packet_type_t type, const char *msg) {
    /* Create packet */
    msg_packet_t pkt = {
        .type      = type,
        .timestamp = (uint32_t)esp_timer_get_time(),
    };

    /* Clear pkt.msg and copy msg to it */
    memset(pkt.msg, 0, sizeof(pkt.msg));
    if (msg)
        strncpy(pkt.msg, msg, sizeof(pkt.msg) - 1);

    /* Add packet to queue */
    xQueueSend(xTelemQueue, &pkt, 0);
}

/* LoRa packet transmission */
static void telem_transmit(sx126x_handle_t lora_handle) {
    msg_packet_t pkt;

    /* Packet on queue -> Transmit packet */
    while (xQueueReceive(xTelemQueue, &pkt, 0) == pdTRUE) {
        bool ok = LoRaSend(lora_handle, (uint8_t *)&pkt, sizeof(msg_packet_t), SX126x_TXMODE_ASYNC);
        if (!ok) {
            ESP_LOGW(TAG_LORA, "Telem drop");
            continue;
        }

        /* Wait for DIO1 notify */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LORA_DIO1_TIMEOUT_MS)) == 0) {
            ESP_LOGW(TAG_LORA, "Telem TX timeout");
            ClearIrqStatus(lora_handle, SX126X_IRQ_ALL);
            lora_handle->txActive = false;
            SetRx(lora_handle, 0xFFFFFF);
            continue;
        }

        uint16_t irq = GetIrqStatus(lora_handle);
        if (irq & SX126X_IRQ_TX_DONE) {
            ReceiveMode(lora_handle);
        } else if (irq & SX126X_IRQ_TIMEOUT) {
            ESP_LOGW(TAG_LORA, "Telem TX timeout");
            ReceiveMode(lora_handle);
        }
        if (irq != 0) {
            ClearIrqStatus(lora_handle, irq);
        }
    }
}

static esp_err_t telem_mode(sx126x_handle_t lora_handle, const EventBits_t bits_to_wait) {
    EventBits_t bits;

    do {
        bits = xEventGroupWaitBits(xStatusEventGroup, bits_to_wait, pdFALSE, pdFALSE, pdMS_TO_TICKS(LORA_TELEM_MS));
        telem_transmit(lora_handle);
    } while (!(bits & (bits_to_wait)));

    /* Inform fatal */
    if (bits & FATAL_ERROR) {
        telem_post(PKT_FAIL, "FATAL_ERR");
        telem_transmit(lora_handle);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t lora_init(sx126x_handle_t *lora_handle) {
    esp_err_t err = ESP_OK;

    /* SX1262 LoRa struct setup */
    sx126x_config_t lora_cfg = {
        .spi_host          = SPI_HOST,
        .ss                = LORA_CS,
        .reset             = LORA_RESET,
        .busy              = LORA_BUSY,
        .txen              = -1,
        .rxen              = -1,
        .frequency         = LORA_FREQUENCY,
        .tx_power          = 22,
        .tcxo_voltage      = 0.0f,
        .use_regulator_ldo = false,
        .spreading_factor  = LORA_SPREADING_FACTOR,
        .bandwidth         = LORA_BANDWIDTH,
        .coding_rate       = LORA_CODING_RATE,
        .preamble_length   = 12,    // 8 -> SF7-8 || 12 -> SF5-6
        .payload_len       = 0,     // Variable length packet
        .crc_on            = true,  // true -> drop garbage
        .invert_iq         = false, // false -> normal communication
    };

    /* SX1262 LoRa initialization */
    err = LoRaInit(&lora_cfg, lora_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LORA, "LoRa init failed: %s", esp_err_to_name(err));
        return err;
    }

    LoRaDebugPrint(*lora_handle, false);
    int16_t lora_ret = LoRaBegin(*lora_handle);
    if (lora_ret != ERR_NONE) {
        ESP_LOGE(TAG_LORA, "LoRa begin failed: %d", lora_ret);
        return ESP_FAIL;
    }

    LoRaConfig(*lora_handle);

    ESP_LOGI(TAG_LORA, "LoRa initialized");
    return ESP_OK;
}

void task_lora(void *pvParameters) {
    esp_err_t       err;
    sx126x_handle_t lora_handle;
    bool            ok;
    uint16_t        lost = 0;

    xTaskLora = xTaskGetCurrentTaskHandle();

    err = lora_init(&lora_handle);
    if (err != ESP_OK) {
        goto setup_error;
    }

    // ADICIONAR BATCH PACKET ---------------------------------------------------------------

    /* SX1262 DIO1 ISR initialization */
    err = gpio_isr_handler_add(LORA_DIO1, dio1_isr_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_LORA, "DIO1 ISR failed: %s", esp_err_to_name(err));
        goto setup_error;
    }

    /* IRQ parameters */
    uint16_t irqMask  = SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT;
    uint16_t dio1Mask = SX126X_IRQ_TX_DONE;
    SetDioIrqParams(lora_handle, irqMask, dio1Mask, 0, 0);

    /* LoRa initialized -> Start packet transmission */
    // xEventGroupSetBits(xInitEventGroup, LORA_INIT);
    telem_post(PKT_INFO, "LORA_RDY");

    /* Telemetry mode - Wait for ARMED */
    err = telem_mode(lora_handle, ARMED | FATAL_ERROR);

    /* Listen for ignition command */
    {
        uint8_t rx_buf[16];
        while (true) {
            uint8_t len = LoRaReceive(lora_handle, rx_buf, sizeof(rx_buf));
            if (len > 0 && rx_buf[0] == CMD_IGNITION) {

                xTaskNotify(xTaskIgnite, EVT_IGNITION_START, eSetValueWithOverwrite);

                uint32_t result;
                ulTaskNotifyValueClear(NULL, 0xFFFFFFFF);
                if (xTaskNotifyWait(0, 0xFFFFFFFF, &result, pdMS_TO_TICKS(200))) {
                    if (result == EVT_IGNITION_SUCCESS) {
                        telem_post(PKT_ACK, "IGNIT_ACK");
                        telem_transmit(lora_handle);
                        break; // go to next phase
                    } else {
                        continue; // loop back
                    }
                } else {
                    ESP_LOGE(TAG_LORA, "Ignition timeout");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    /* Wait for acquisition to finish */
    xEventGroupWaitBits(xStatusEventGroup, SAVE_DATA, pdFALSE, pdTRUE, portMAX_DELAY);

    /* Telemetry mode - Wait for SEND_DATA */
    err = telem_mode(lora_handle, SEND_DATA);

    /* Data packet number - Track lost packets */
    static uint16_t pkt_num = 0;

    /* Send ADS samples */
    for (uint32_t i = 0; i < sys_data_g.ads_sample; i++) {

        ads_packet_t ads_packet = {
            .header = {.type = PKT_ADS, .seq = pkt_num++},
            .data   = ads_data_g[i],
        };

        ok = LoRaSend(lora_handle, (uint8_t *)&ads_packet, sizeof(ads_packet), SX126x_TXMODE_ASYNC);
        if (!ok) {
            ESP_LOGI(TAG_LORA, "Sample %lu lost.", i);
            continue;
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LORA_DIO1_TIMEOUT_MS)) == 0) {
            ESP_LOGW(TAG_LORA, "TX notify timeout at sample %lu", i);
            lost++;
            ClearIrqStatus(lora_handle, SX126X_IRQ_ALL);
            lora_handle->txActive = false;
            SetRx(lora_handle, 0xFFFFFF);
            continue;
        }

        uint16_t irq = GetIrqStatus(lora_handle);
        if (irq & SX126X_IRQ_TX_DONE) {
            ReceiveMode(lora_handle);
        } else if (irq & SX126X_IRQ_TIMEOUT) {
            ESP_LOGW(TAG_LORA, "TX timeout at sample %lu", i);
            ReceiveMode(lora_handle);
        }
        if (irq != 0) {
            ClearIrqStatus(lora_handle, irq);
        }
    }
    uint16_t ads_lost = GetPacketLost(lora_handle);
    ESP_LOGI(TAG_LORA, "ADS samples lost: %u", ads_lost);

    /* Send MAX samples */
    for (uint32_t i = 0; i < sys_data_g.max_sample; i++) {

        max_packet_t max_packet = {
            .header = {.type = PKT_MAX, .seq = pkt_num++},
            .data   = max_data_g[i],
        };

        ok = LoRaSend(lora_handle, (uint8_t *)&max_packet, sizeof(max_packet), SX126x_TXMODE_ASYNC);
        if (!ok) {
            ESP_LOGI(TAG_LORA, "Sample %lu lost.", i);
            continue;
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LORA_DIO1_TIMEOUT_MS)) == 0) {
            ESP_LOGW(TAG_LORA, "TX notify timeout at sample %lu", i);
            lost++;
            ClearIrqStatus(lora_handle, SX126X_IRQ_ALL);
            lora_handle->txActive = false;
            SetRx(lora_handle, 0xFFFFFF);
            continue;
        }

        uint16_t irq = GetIrqStatus(lora_handle);
        if (irq & SX126X_IRQ_TX_DONE) {
            ReceiveMode(lora_handle);
        } else if (irq & SX126X_IRQ_TIMEOUT) {
            ESP_LOGW(TAG_LORA, "TX timeout at sample %lu", i);
            ReceiveMode(lora_handle);
        }
        if (irq != 0) {
            ClearIrqStatus(lora_handle, irq);
        }
    }

    uint16_t max_lost = GetPacketLost(lora_handle) - ads_lost;
    ESP_LOGI(TAG_LORA, "MAX samples lost: %u", max_lost);

    ESP_LOGI(TAG_LORA, "SEND_DATA complete");

    status_event_t evt = EVT_SEND_DONE;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);

    telem_transmit(lora_handle);
cleanup:
    gpio_intr_disable(LORA_DIO1);
    gpio_isr_handler_remove(LORA_DIO1);

    vTaskDelete(NULL);

setup_error: {
    ESP_LOGE(TAG_LORA, "LoRa init failed: %s", esp_err_to_name(err));

    status_event_t evt = EVT_SETUP_FAILED;
    xQueueSend(xEventQueue, &evt, portMAX_DELAY);
}

    goto cleanup;
}