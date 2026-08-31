// main_original_lvgl.cpp
// Original esp_lcd ILI9341 driver + LVGL v9 integration.
// Requires component "lvgl/lvgl" (see idf_component.yml note at bottom of this file).
//
// Pin map / SPI host chosen to match the debugging session on
// main_original_esp_lcd.cpp: SPI3_HOST (VSPI native IOMUX) so we can run at
// full 40MHz without the GPIO-matrix clock cap. RGB order / invert are left
// as TODO - plug in whatever combo you confirmed works on your panel.

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"   // esp_lcd_new_panel_ili9341() lives here (managed component)
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

static const char *TAG = "orig_lvgl";

// ---- Pin map: adjust to your board ----
#define PIN_LCD_MOSI   23
#define PIN_LCD_SCLK   18
#define PIN_LCD_CS     5
#define PIN_LCD_DC     4
#define PIN_LCD_RST    13
#define PIN_LCD_BL     27
#define LCD_SPI_HOST   SPI2_HOST   // matches VSPI native IOMUX for pins above
#define LCD_PIXEL_CLK_HZ (20 * 1000 * 1000)

#define LCD_H_RES      320
#define LCD_V_RES      240
#define LVGL_DRAW_BUF_LINES  40     // partial buffer height, tune for free RAM

static esp_lcd_panel_handle_t s_panel_handle = nullptr;
static lv_display_t *s_disp = nullptr;

// ---- esp_lcd -> LVGL glue ----

// Called by esp_lcd once the queued SPI transaction for a flush has actually
// finished on the wire. This runs in ISR context (SPI post-transfer callback).
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                           esp_lcd_panel_io_event_data_t *edata,
                                           void *user_ctx)
{
    lv_display_t *disp = static_cast<lv_display_t *>(user_ctx);
    lv_display_flush_ready(disp);
    return false; // no higher-priority task woken here
}

// LVGL calls this whenever it has a rendered region ready to push to the panel.
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // esp_lcd_panel_draw_bitmap() queues the SPI transfer and returns
    // immediately; actual completion (and lv_display_flush_ready()) happens
    // in on_color_trans_done() above. This lets LVGL keep rendering the next
    // frame while the SPI transfer for this one is still in flight.
    esp_lcd_panel_draw_bitmap(s_panel_handle,
                               area->x1, area->y1,
                               area->x2 + 1, area->y2 + 1,
                               px_map);
}

// Periodic tick source LVGL needs to track animations/timers.
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(2); // must match the esp_timer period set up below (2ms)
}

// ---- Demo UI ----
static void build_demo_ui(void)
{
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "ILI9341 + LVGL v9\n(esp_lcd driver)");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

extern "C" void app_main(void)
{
    // 0. Backlight - off during init, on once panel + LVGL are ready
    gpio_config_t bl_conf = {};
    bl_conf.pin_bit_mask = 1ULL << PIN_LCD_BL;
    bl_conf.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl_conf);
    gpio_set_level((gpio_num_t)PIN_LCD_BL, 0);

    // 1. SPI bus
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = PIN_LCD_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = PIN_LCD_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * sizeof(uint16_t);

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Panel IO
    esp_lcd_panel_io_handle_t io_handle = nullptr;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = PIN_LCD_DC;
    io_config.cs_gpio_num = PIN_LCD_CS;
    io_config.pclk_hz = LCD_PIXEL_CLK_HZ;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = on_color_trans_done;
    // user_ctx for the callback is set to the lv_display_t* right after we create it below

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io_handle));

    // 3. Panel
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIN_LCD_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; // TODO: confirm on your module
    panel_config.bits_per_pixel = 16;

    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, false)); // TODO: confirm on your module
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, true, false)); // fixes mirrored/backwards text -
    // try (false,true) or (true,true) if this combo isn't the right one
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // 4. LVGL init
    lv_init();

    // Draw buffers: partial-height, DMA-capable. Two buffers lets esp_lcd
    // transfer one while LVGL renders into the other.
    size_t buf_pixels = LCD_H_RES * LVGL_DRAW_BUF_LINES;
    lv_color_t *buf1 = static_cast<lv_color_t *>(
        heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA));
    lv_color_t *buf2 = static_cast<lv_color_t *>(
        heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA));
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        return;
    }

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp, buf1, buf2,
                            buf_pixels * sizeof(lv_color_t),
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    // Wire the trans-done callback's user_ctx to this display now that it exists
    esp_lcd_panel_io_callbacks_t cbs = {};
    cbs.on_color_trans_done = on_color_trans_done;
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, s_disp));

    // 5. LVGL tick source (esp_timer, 2ms period)
    esp_timer_handle_t lvgl_tick_timer;
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = lvgl_tick_cb;
    timer_args.name = "lvgl_tick";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, 2 * 1000)); // 2ms in us

    // 6. Backlight on, build UI
    gpio_set_level((gpio_num_t)PIN_LCD_BL, 1);
    build_demo_ui();

    ESP_LOGI(TAG, "LVGL + esp_lcd ready");

    // 7. LVGL runs cooperatively - pump the timer handler from a task loop.
    //    For anything beyond this demo, move UI updates into LVGL timers/tasks
    //    and guard lv_* calls with a mutex if touched from other tasks.
    while (true) {
        uint32_t sleep_ms = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(sleep_ms ? sleep_ms : 5));
    }
}

// ---- idf_component.yml (place next to this main/'s CMakeLists.txt) ----
// dependencies:
//   lvgl/lvgl: "^9.2"