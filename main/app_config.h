    /**
 * @file app_config.h
 * @brief Central configuration for the OTA test demo.
 *
 * Edit WIFI_SSID / WIFI_PASS and OTA_FIRMWARE_URL to match your setup.
 */
#pragma once

// ---- Wi-Fi credentials ----
#define WIFI_SSID       "B9 106"
#define WIFI_PASS       "B91062005@"
#define WIFI_MAXIMUM_RETRY  5

// ---- OTA firmware source (GitHub Pages, HTTPS) ----
// Example: https://<username>.github.io/<repo>/firmware.bin
#define OTA_FIRMWARE_URL "https://hphuc15.github.io/ota_demo/firmware.bin"

// HTTP receive buffer / timeout tuning
#define OTA_RECV_TIMEOUT_MS  10000
