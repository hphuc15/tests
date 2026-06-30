/**
 * @file main.c
 * @brief OTA test demo entry point.
 *
 * Flow:
 *   1. Connect to Wi-Fi (blocking).
 *   2. Confirm this boot is good (cancels pending rollback, if any).
 *   3. Print current firmware version.
 *   4. Start interactive console — type "ota" to trigger an HTTPS OTA
 *      update from the URL configured in app_config.h (GitHub Pages demo).
 */
#include "esp_log.h"
#include "esp_app_desc.h"

#include "wifi_connect.h"
#include "ota_update.h"
#include "console_cmds.h"

static const char *TAG = "main";

void app_main(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " OTA Test Demo - firmware version: %s", app_desc->version);
    ESP_LOGI(TAG, "==============================================");

    if (!wifi_connect_start()) {
        ESP_LOGE(TAG, "Wi-Fi connection failed. OTA will not be reachable.");
        // Continue anyway so the console is still usable for debugging.
    }

    // We booted fine and (hopefully) have network — cancel any pending
    // rollback so the bootloader doesn't revert this image on next reset.
    ota_confirm_boot_ok();

    // Blocks forever, handling "ota" / "version" / "reboot" / "help" commands.
    console_start();
}
