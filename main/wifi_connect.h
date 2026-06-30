/**
 * @file wifi_connect.h
 * @brief Minimal Wi-Fi STA connection helper for the OTA demo.
 */
#pragma once

#include <stdbool.h>

/**
 * @brief Initialize Wi-Fi in station mode and connect using credentials
 *        from app_config.h. Blocks until connected or all retries fail.
 *
 * @return true if connected and got an IP address, false otherwise.
 */
bool wifi_connect_start(void);
