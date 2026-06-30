/**
 * @file lora_platform_esp32.c
 * @brief ESP-IDF platform implementation for the LoRa SX1278 driver.
 *
 * Implements all LoRa_Platform_* extern functions declared in LoRa.h
 * using ESP-IDF SPI master driver, GPIO, and FreeRTOS primitives.
 *
 * Pin mapping (default):
 *   MOSI  -> GPIO 23
 *   MISO  -> GPIO 19
 *   SCK   -> GPIO 18
 *   NSS   -> GPIO 5
 *   RST   -> GPIO 27
 *   DIO0  -> GPIO 26
 */
#include "LoRa.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ──────────────── Pin Configuration ──────────────── */

#define LORA_PIN_MOSI       23
#define LORA_PIN_MISO       19
#define LORA_PIN_SCK        18
#define LORA_PIN_NSS         5
#define LORA_PIN_RST        27
#define LORA_PIN_DIO0       26

#define LORA_SPI_HOST       SPI2_HOST
#define LORA_SPI_CLOCK_HZ   (1 * 1000 * 1000)   /* 1 MHz – safe for SX1278 */

static const char* TAG = "lora_platform";

/* SPI device handle – initialized once in LoRa_Platform_SPI_Init() */
static spi_device_handle_t s_spi_handle = NULL;

/* DIO0 interrupt flag – set by ISR, cleared by gateway logic */
volatile uint8_t g_dio0_flag = 0;

/* ──────────────── DIO0 ISR ──────────────── */

static void IRAM_ATTR dio0_isr_handler(void* arg)
{
    (void)arg;
    g_dio0_flag = 1;
}

/* ──────────────── Platform Functions ──────────────── */

/**
 * @brief Initialize SPI bus and add the SX1278 as a device.
 *        Also configures RST and DIO0 GPIOs.
 */
void LoRa_Platform_SPI_Init(void)
{
    /* ── SPI bus ── */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = LORA_PIN_MOSI,
        .miso_io_num     = LORA_PIN_MISO,
        .sclk_io_num     = LORA_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 256,
    };
    esp_err_t ret = spi_bus_initialize(LORA_SPI_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return;
    }

    spi_device_interface_config_t dev_cfg = {
        .mode           = 0,                    /* CPOL=0, CPHA=0 */
        .clock_speed_hz = LORA_SPI_CLOCK_HZ,
        .spics_io_num   = -1,                   /* Manual CS via NSS pin */
        .queue_size     = 1,
    };
    ret = spi_bus_add_device(LORA_SPI_HOST, &dev_cfg, &s_spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed: %s", esp_err_to_name(ret));
        return;
    }

    /* ── NSS pin (output, default HIGH) ── */
    gpio_config_t nss_cfg = {
        .pin_bit_mask = (1ULL << LORA_PIN_NSS),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&nss_cfg);
    gpio_set_level(LORA_PIN_NSS, 1);

    /* ── RST pin (output, default HIGH) ── */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << LORA_PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(LORA_PIN_RST, 1);

    /* ── DIO0 pin (input, rising edge interrupt) ── */
    gpio_config_t dio0_cfg = {
        .pin_bit_mask = (1ULL << LORA_PIN_DIO0),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    gpio_config(&dio0_cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(LORA_PIN_DIO0, dio0_isr_handler, NULL);

    ESP_LOGI(TAG, "SPI & GPIO initialized (NSS=%d, RST=%d, DIO0=%d)",
             LORA_PIN_NSS, LORA_PIN_RST, LORA_PIN_DIO0);
}

/**
 * @brief Full-duplex SPI transfer of a single byte.
 */
uint8_t LoRa_Platform_SPI_Transfer(uint8_t val)
{
    uint8_t rx = 0;
    spi_transaction_t txn;
    memset(&txn, 0, sizeof(txn));
    txn.length    = 8;          /* 8 bits */
    txn.tx_buffer = &val;
    txn.rx_buffer = &rx;

    spi_device_transmit(s_spi_handle, &txn);
    return rx;
}

void LoRa_Platform_NSS_Low(void)
{
    gpio_set_level(LORA_PIN_NSS, 0);
}

void LoRa_Platform_NSS_High(void)
{
    gpio_set_level(LORA_PIN_NSS, 1);
}

void LoRa_Platform_RESET_Low(void)
{
    gpio_set_level(LORA_PIN_RST, 0);
}

void LoRa_Platform_RESET_High(void)
{
    gpio_set_level(LORA_PIN_RST, 1);
}

void LoRa_Platform_DelayMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t LoRa_Platform_GetTickMs(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
