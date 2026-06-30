/**
 * @file lora_gateway.h
 * @brief LoRa Gateway component public API.
 *
 * Provides a single-call interface to start the LoRa Gateway on ESP32 Core 1.
 * All SPI/GPIO initialization, LoRa SX1278 configuration, beacon transmission,
 * and sensor data reception are handled internally.
 */
#ifndef LORA_GATEWAY_H
#define LORA_GATEWAY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the LoRa Gateway on Core 1.
 *
 * Creates a FreeRTOS task pinned to Core 1 that:
 *   1. Initializes SPI bus and GPIO for the SX1278 module
 *   2. Configures the LoRa transceiver (433 MHz, SF7, BW125K, 20 dBm)
 *   3. Periodically broadcasts sync beacons every 30 seconds
 *   4. Continuously receives and parses sensor packets from nodes
 *
 * This function returns immediately after creating the task.
 */
void lora_gateway_start(void);

#ifdef __cplusplus
}
#endif

#endif /* LORA_GATEWAY_H */
