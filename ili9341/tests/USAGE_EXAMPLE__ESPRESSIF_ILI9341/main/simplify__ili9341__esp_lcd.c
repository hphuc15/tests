/*
 * example_main.c
 *
 * Test toàn bộ component ili9341_driver (thay thế esp_lcd + espressif/esp_lcd_ili9341).
 * Chỉ dùng driver/spi_master.h (chuẩn IDF) + component "ili9341" của bạn.
 * KHÔNG include bất kỳ header esp_lcd_* nào.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#include "ili9341_driver.h"

static const char *TAG = "example";

/* ==== Cấu hình board thực tế ==== */
#define PIN_LCD_MOSI     23
#define PIN_LCD_SCLK     18
#define PIN_LCD_CS       5
#define PIN_LCD_DC       4
#define PIN_LCD_RST      13
#define PIN_LCD_BL       27
#define LCD_SPI_HOST     SPI2_HOST   // matches VSPI native IOMUX for pins above
#define LCD_PIXEL_CLK_HZ (20 * 1000 * 1000)

#define LCD_H_RES        320
#define LCD_V_RES        240
#define LCD_PIXEL_BITS   16   // RGB565

static ili9341_panel_handle_t s_panel = NULL;
static ili9341_io_handle_t s_io = NULL;

/* Callback báo hoàn tất truyền màu (chạy trong ISR context) */
static bool notify_color_trans_done(ili9341_io_handle_t panel_io, ili9341_io_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    return false; // không cần yield task cao hơn
}

/* Fill toàn màn hình 1 màu RGB565 để test draw_bitmap + SPI transport
 * LƯU Ý: ILI9341 nhận dữ liệu màu 16-bit theo thứ tự big-endian (byte cao trước),
 * nhưng ESP32 là CPU little-endian nên phải tự đảo byte trước khi đưa vào buffer,
 * vì ili9341_io_tx_color() gửi thẳng buffer, không tự đảo byte màu.
 */
static void fill_screen(uint16_t color)
{
    const int line_pixels = LCD_H_RES;
    const uint16_t color_be = __builtin_bswap16(color); // đảo byte cho đúng thứ tự SPI
    uint16_t *line_buf = heap_caps_malloc(line_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line_buf) {
        ESP_LOGE(TAG, "no mem for line buffer");
        return;
    }
    for (int i = 0; i < line_pixels; i++) {
        line_buf[i] = color_be;
    }
    for (int y = 0; y < LCD_V_RES; y++) {
        ili9341_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + 1, line_buf);
    }
    heap_caps_free(line_buf);
}

/* Bật backlight qua GPIO thường (không PWM, chỉ test on/off) */
static void backlight_init(void)
{
    gpio_config_t bk_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_LCD_BL,
    };
    gpio_config(&bk_conf);
    gpio_set_level(PIN_LCD_BL, 1); // bật đèn nền
}

void app_main(void)
{
    ESP_LOGI(TAG, "0. Backlight on (GPIO%d)", PIN_LCD_BL);
    backlight_init();

    ESP_LOGI(TAG, "1. Init SPI bus (SCLK=%d MOSI=%d)", PIN_LCD_SCLK, PIN_LCD_MOSI);
    spi_bus_config_t buscfg = ILI9341_PANEL_BUS_SPI_CONFIG(PIN_LCD_SCLK, PIN_LCD_MOSI, LCD_H_RES * 80 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "2. Attach LCD to SPI bus (ili9341_new_io_spi), pclk=%d Hz", LCD_PIXEL_CLK_HZ);
    ili9341_io_spi_config_t io_config = ILI9341_PANEL_IO_SPI_CONFIG(PIN_LCD_CS, PIN_LCD_DC, notify_color_trans_done, NULL);
    io_config.pclk_hz = LCD_PIXEL_CLK_HZ; // override tốc độ mặc định 40MHz của macro
    ESP_ERROR_CHECK(ili9341_new_io_spi((ili9341_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &s_io));

    ESP_LOGI(TAG, "3. Install ILI9341 panel driver (ili9341_new_panel)");
    ili9341_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = ILI9341_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = LCD_PIXEL_BITS,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = NULL, // NULL => dùng vendor_specific_init_default trong driver
    };
    ESP_ERROR_CHECK(ili9341_new_panel(s_io, &panel_config, &s_panel));

    ESP_LOGI(TAG, "4. Reset + init panel");
    ESP_ERROR_CHECK(ili9341_panel_reset(s_panel));
    ESP_ERROR_CHECK(ili9341_panel_init(s_panel));

    ESP_LOGI(TAG, "5. Orientation (landscape %dx%d) + turn on display", LCD_H_RES, LCD_V_RES);
    // ILI9341 gốc là panel dọc 240x320 -> cần swap_xy để ra ngang 320x240
    ESP_ERROR_CHECK(ili9341_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(ili9341_panel_mirror(s_panel, false, false));
    ESP_ERROR_CHECK(ili9341_panel_invert_color(s_panel, false));
    ESP_ERROR_CHECK(ili9341_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "6. Draw test pattern (fill red/green/blue/white)");
    while (1) {
        fill_screen(0xF800); // Red (RGB565)
        vTaskDelay(pdMS_TO_TICKS(800));
        fill_screen(0x07E0); // Green
        vTaskDelay(pdMS_TO_TICKS(800));
        fill_screen(0x001F); // Blue
        vTaskDelay(pdMS_TO_TICKS(800));
        fill_screen(0xFFFF); // White
        vTaskDelay(pdMS_TO_TICKS(800));
        ESP_LOGI(TAG, "loop ok - draw_bitmap + SPI transport hoat dong binh thuong");
    }
}