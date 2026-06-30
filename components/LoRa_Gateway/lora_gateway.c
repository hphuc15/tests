/**
 * @file lora_gateway.c
 * @brief LoRa Gateway logic for ESP32 – runs as a FreeRTOS task on Core 1.
 *
 * Ported from the STM32 test code (tests/WSN/LoRa_Gateway/LoRa_gateway.c).
 * Functionality:
 *   - Broadcasts a sync beacon every 30 seconds
 *   - Receives sensor data packets from nodes via DIO0 interrupt
 *   - Logs received data (node_id, temperature, humidity) via ESP_LOGI
 */
#include "lora_gateway.h"
#include "LoRa.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "lora_gateway";

/* ──────────────── Packet Definitions ──────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  node_id;
    int16_t  temperature;
    int16_t  humidity_air;
    int16_t  humidity_soil;
} SensorPacket_t;

typedef struct __attribute__((packed)) {
    uint8_t  broadcast_id;
    uint8_t  command_code;
    uint16_t network_cycle_s;
    uint16_t slot_duration_ms;
} BeaconPacket_t;

/* ──────────────── DIO0 flag (set by ISR in platform layer) ──────────────── */

extern volatile uint8_t g_dio0_flag;

/* ──────────────── Gateway Configuration ──────────────── */

#define BEACON_INTERVAL_MS    30000U
#define GATEWAY_TASK_STACK    4096
#define GATEWAY_TASK_PRIORITY 5
#define GATEWAY_TASK_CORE     1

/* ──────────────── Gateway Task ──────────────── */

static void lora_gateway_task(void* pvParameters)
{
    (void)pvParameters;

    /* ── LoRa SX1278 Configuration ── */
    LoRa_Config_t gw_config = {
        .frequency             = 433000000UL,
        .preamble              = 10,
        .spredingFactor        = 7,
        .bandWidth             = BW_125KHz,
        .crcRate               = CR_4_5,
        .power                 = 20,
        .overCurrentProtection = 120,
        .syncWord              = 0x12,
        .enableCRC             = 1,
        .implicitHeader        = 0,
    };

    if (LoRa_Init(&gw_config) != LORA_OK) {
        ESP_LOGE(TAG, "Gateway Init Failed!");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Gateway Init OK. Starting Master Clock...");

    /* Force an immediate beacon on first iteration */
    uint32_t last_beacon_tick = LoRa_Platform_GetTickMs() - BEACON_INTERVAL_MS;

    /* ── Main Loop ── */
    for (;;) {

        /* 1. Periodic Beacon Transmission */
        uint32_t now = LoRa_Platform_GetTickMs();
        if ((now - last_beacon_tick) >= BEACON_INTERVAL_MS) {
            last_beacon_tick = now;

            BeaconPacket_t beacon = {
                .broadcast_id     = 0x00,
                .command_code     = 150,
                .network_cycle_s  = 30,
                .slot_duration_ms = 300,
            };

            LoRa_Transmit(&gw_config, (uint8_t*)&beacon, sizeof(BeaconPacket_t), 200);
            ESP_LOGI(TAG, "[MASTER] Broadcasted Sync Beacon. Listening...");

            /* Flush FIFO pointer before entering RX mode */
            LoRa_WriteReg(REG_FIFO_ADDR_PTR, 0x00);
            LoRa_RxStart(&gw_config);
            g_dio0_flag = 0;
        }

        /* 2. Deferred Interrupt Handling for RX Processing */
        if (g_dio0_flag == 1) {
            g_dio0_flag = 0;

            uint8_t irq_flags = LoRa_ReadReg(REG_IRQ_FLAGS);
            LoRa_WriteReg(REG_IRQ_FLAGS, irq_flags);

            if (irq_flags & 0x20) {
                /* CRC Error */
                ESP_LOGW(TAG, "[DROP] CRC Error");
            }
            else if (irq_flags & 0x40) {
                /* RxDone – switch to Standby to lock FIFO */
                LoRa_WriteReg(REG_OP_MODE, 0x80 | 0x08 | 0x01);

                uint8_t pkt_len   = LoRa_ReadReg(REG_RX_NB_BYTES);
                uint8_t fifo_addr = LoRa_ReadReg(REG_FIFO_RX_CURRENT);

                /* Peek first byte to filter self-broadcasted beacon echoes */
                LoRa_WriteReg(REG_FIFO_ADDR_PTR, fifo_addr);
                uint8_t first_byte = LoRa_ReadReg(REG_FIFO);

                if (first_byte != 0x00 && pkt_len == sizeof(SensorPacket_t)) {
                    SensorPacket_t rx_data;

                    /* Reset FIFO pointer and burst read */
                    LoRa_WriteReg(REG_FIFO_ADDR_PTR, fifo_addr);
                    LoRa_BurstRead(REG_FIFO, (uint8_t*)&rx_data, pkt_len);

                    int16_t rssi = LoRa_GetPacketRSSI();

                    ESP_LOGI(TAG,
                        "RX OK | Node:%d | T:%d.%d C | A:%d.%d%% | S:%d.%d%% | RSSI:%d",
                        rx_data.node_id,
                        rx_data.temperature   / 100, rx_data.temperature   % 100,
                        rx_data.humidity_air   / 100, rx_data.humidity_air  % 100,
                        rx_data.humidity_soil  / 100, rx_data.humidity_soil % 100,
                        rssi);
                }
            }

            /* Mandatory FIFO cleanup */
            LoRa_WriteReg(REG_FIFO_ADDR_PTR, 0x00);
            LoRa_RxStart(&gw_config);
        }

        /* Yield CPU – 10 ms polling interval */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ──────────────── Public API ──────────────── */

void lora_gateway_start(void)
{
    xTaskCreatePinnedToCore(
        lora_gateway_task,
        "lora_gw",
        GATEWAY_TASK_STACK,
        NULL,
        GATEWAY_TASK_PRIORITY,
        NULL,
        GATEWAY_TASK_CORE
    );

    ESP_LOGI(TAG, "Gateway task created on Core %d", GATEWAY_TASK_CORE);
}
