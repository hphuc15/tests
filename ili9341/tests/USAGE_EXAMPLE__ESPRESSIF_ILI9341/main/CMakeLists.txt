# test espressif_ili9341 + esp_lcd + lvgl
idf_component_register(
    SRCS            "example_main.cpp"
    INCLUDE_DIRS    "."
    REQUIRES        driver esp_lcd esp_timer
)

# test giản lược espressif_ili9341 + esp_lcd (C)
idf_component_register(
    SRCS            "simplify__ili9341__esp_lcd.c" "SIMPLIFY__ESPRESSIF_ILI9341__ESP_LCD/ili9341_driver.c"
    INCLUDE_DIRS    "." "CLAUDE_CLEAN"
    REQUIRES        driver esp_timer
)


