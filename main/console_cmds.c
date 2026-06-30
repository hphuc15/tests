/**
 * @file console_cmds.c
 * @brief Registers interactive console commands for the OTA demo
 *        (e.g. "ota" to trigger an update from the Serial Monitor).
 */
#include "console_cmds.h"
#include "ota_update.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

static const char *TAG = "console_cmds";

// ---- "ota" command: trigger OTA update over HTTPS ----
static int cmd_ota(int argc, char **argv)
{
    ESP_LOGI(TAG, "OTA command received, starting update...");
    esp_err_t err = ota_perform_update();
    // Only reached if OTA failed before reboot.
    ESP_LOGE(TAG, "OTA did not complete (err=0x%x). Device NOT rebooted.", err);
    return 1;
}

// ---- "version" command: print current running firmware version ----
static int cmd_version(int argc, char **argv)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    printf("Firmware version : %s\n", app_desc->version);
    printf("Compile time     : %s %s\n", app_desc->date, app_desc->time);
    printf("IDF version      : %s\n", app_desc->idf_ver);
    return 0;
}

// ---- "reboot" command: convenience for testing ----
static int cmd_reboot(int argc, char **argv)
{
    printf("Rebooting...\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

static void register_commands(void)
{
    const esp_console_cmd_t ota_cmd = {
        .command = "ota",
        .help = "Trigger an HTTPS OTA update from the configured firmware URL",
        .hint = NULL,
        .func = &cmd_ota,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ota_cmd));

    const esp_console_cmd_t version_cmd = {
        .command = "version",
        .help = "Print the currently running firmware version",
        .hint = NULL,
        .func = &cmd_version,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&version_cmd));

    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Reboot the device",
        .hint = NULL,
        .func = &cmd_reboot,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "ota_demo>";
    repl_config.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    esp_console_register_help_command();
    register_commands();

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "Console ready. Type 'help' to list commands, 'ota' to trigger update.");
}
