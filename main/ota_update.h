/**
 * @file ota_update.h
 * @brief HTTPS OTA update routine using esp_https_ota, with progress logging
 *        and rollback-safe boot confirmation.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Mark the currently running app partition as valid, cancelling any
 *        pending rollback. Call this once your app confirms it booted and
 *        is working correctly (e.g. after Wi-Fi connects successfully).
 */
void ota_confirm_boot_ok(void);

/**
 * @brief Perform an HTTPS OTA update from OTA_FIRMWARE_URL (app_config.h).
 *        Blocks until the download/flash completes, then reboots on success.
 *        Runs synchronously in the caller's task — call from a console
 *        command handler or a dedicated task, not from a timer/ISR context.
 *
 * @return ESP_OK only if something went wrong before reboot could happen;
 *         on success the device reboots and this function does not return.
 */
esp_err_t ota_perform_update(void);
