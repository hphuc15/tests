idf_component_register(
    SRCS "main.c" "wifi_connect.c" "ota_update.c" "console_cmds.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash esp_https_ota esp_http_client esp-tls
             console app_update mbedtls
)
