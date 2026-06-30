/**
 * @file ota_update.c
 * @brief HTTPS OTA update routine using esp_https_ota, with progress logging
 *        and rollback-safe boot confirmation.
 */
#include "ota_update.h"
#include "app_config.h"

#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "ota_update";

void ota_confirm_boot_ok(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "New app booted OK, cancelling rollback for partition '%s'",
                     running->label);
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

esp_err_t ota_perform_update(void)
{
    ESP_LOGI(TAG, "Starting OTA from: %s", OTA_FIRMWARE_URL);

    esp_http_client_config_t http_config = {
        .url = OTA_FIRMWARE_URL,
        // GitHub Pages serves a publicly-trusted cert (Let's Encrypt/DigiCert),
        // so the built-in IDF cert bundle is enough — no need to embed a .pem.
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = OTA_RECV_TIMEOUT_MS,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .partial_http_download = false,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "New firmware version: %s", app_desc.version);
    } else {
        ESP_LOGW(TAG, "Could not read incoming app description: %s", esp_err_to_name(err));
    }

    int last_logged_percent = -1;
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        int total = esp_https_ota_get_image_size(https_ota_handle);
        int read = esp_https_ota_get_image_len_read(https_ota_handle);
        if (total > 0) {
            int percent = (read * 100) / total;
            if (percent != last_logged_percent) {
                ESP_LOGI(TAG, "OTA progress: %d%% (%d/%d bytes)", percent, read, total);
                last_logged_percent = percent;
            }
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA download/flash failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        return err;
    }

    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "OTA data incomplete — refusing to finalize");
        esp_https_ota_abort(https_ota_handle);
        return ESP_FAIL;
    }

    err = esp_https_ota_finish(https_ota_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed — corrupted or wrong image");
        } else {
            ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    ESP_LOGI(TAG, "OTA Success! Rebooting in 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    // Unreachable
    return ESP_OK;
}
