/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "hal/dma_types.h"
#include "esp_intr_alloc.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_assert.h"
#include "hal/lcd_types.h"
#include "hal/color_types.h"
#include "driver/spi_master.h"
#if SOC_LCDCAM_SUPPORTED
#include "hal/lcd_hal.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Version Info                                                              */
/* ========================================================================= */
#define ILI9341_DRV_VER_MAJOR 1
#define ILI9341_DRV_VER_MINOR 0
#define ILI9341_DRV_VER_PATCH 0

/* ========================================================================= */
/* Platform Resource Manager                                                 */
/* ========================================================================= */
#if SOC_LCDCAM_SUPPORTED
typedef enum {
    ILI9341_COM_DEVICE_TYPE_I80,
    ILI9341_COM_DEVICE_TYPE_RGB
} ili9341_com_device_type_t;

int ili9341_com_register_device(ili9341_com_device_type_t device_type, void *device_obj);
void ili9341_com_remove_device(ili9341_com_device_type_t device_type, int member_id);
#endif // SOC_LCDCAM_SUPPORTED

static inline void ili9341_com_reverse_buffer_bytes(uint8_t *buf, int start, int end)
{
    uint8_t temp = 0;
    while (start < end) {
        temp = buf[start];
        buf[start] = buf[end];
        buf[end] = temp;
        start++;
        end--;
    }
}

/* ========================================================================= */
/* LCD Commands & Bitmasks (DCS commands, own namespace)                     */
/* ========================================================================= */
#define ILI9341_CMD_NOP          0x00
#define ILI9341_CMD_SWRESET      0x01
#define ILI9341_CMD_RDDID        0x04
#define ILI9341_CMD_RDDST        0x09
#define ILI9341_CMD_RDDPM        0x0A
#define ILI9341_CMD_RDD_MADCTL   0x0B
#define ILI9341_CMD_RDD_COLMOD   0x0C
#define ILI9341_CMD_RDDIM        0x0D
#define ILI9341_CMD_RDDSM        0x0E
#define ILI9341_CMD_RDDSR        0x0F
#define ILI9341_CMD_SLPIN        0x10
#define ILI9341_CMD_SLPOUT       0x11
#define ILI9341_CMD_PTLON        0x12
#define ILI9341_CMD_NORON        0x13
#define ILI9341_CMD_INVOFF       0x20
#define ILI9341_CMD_INVON        0x21
#define ILI9341_CMD_GAMSET       0x26
#define ILI9341_CMD_DISPOFF      0x28
#define ILI9341_CMD_DISPON       0x29
#define ILI9341_CMD_CASET        0x2A
#define ILI9341_CMD_RASET        0x2B
#define ILI9341_CMD_RAMWR        0x2C
#define ILI9341_CMD_RAMRD        0x2E
#define ILI9341_CMD_PTLAR        0x30
#define ILI9341_CMD_VSCRDEF      0x33
#define ILI9341_CMD_TEOFF        0x34
#define ILI9341_CMD_TEON         0x35
#define ILI9341_CMD_MADCTL       0x36
#define ILI9341_CMD_VSCSAD       0x37
#define ILI9341_CMD_IDMOFF       0x38
#define ILI9341_CMD_IDMON        0x39
#define ILI9341_CMD_COLMOD       0x3A
#define ILI9341_CMD_RAMWRC       0x3C
#define ILI9341_CMD_RAMRDC       0x3E
#define ILI9341_CMD_STE          0x44
#define ILI9341_CMD_GDCAN        0x45
#define ILI9341_CMD_WRDISBV      0x51
#define ILI9341_CMD_RDDISBV      0x52

#define ILI9341_CMD_MH_BIT       (1 << 2)
#define ILI9341_CMD_BGR_BIT      (1 << 3)
#define ILI9341_CMD_ML_BIT       (1 << 4)
#define ILI9341_CMD_MV_BIT       (1 << 5)
#define ILI9341_CMD_MX_BIT       (1 << 6)
#define ILI9341_CMD_MY_BIT       (1 << 7)

/* ========================================================================= */
/* Basic Types                                                                */
/* ========================================================================= */
typedef struct ili9341_io_t *ili9341_io_handle_t;
typedef struct ili9341_panel_t *ili9341_panel_handle_t;

typedef enum {
    ILI9341_RGB_ELEMENT_ORDER_RGB = COLOR_RGB_ELEMENT_ORDER_RGB,
    ILI9341_RGB_ELEMENT_ORDER_BGR = COLOR_RGB_ELEMENT_ORDER_BGR,
} ili9341_rgb_element_order_t;

typedef struct {
} ili9341_io_event_data_t;

typedef bool (*ili9341_io_color_trans_done_cb_t)(ili9341_io_handle_t panel_io, ili9341_io_event_data_t *edata, void *user_ctx);

typedef struct {
    ili9341_io_color_trans_done_cb_t on_color_trans_done;
} ili9341_io_callbacks_t;

/* ========================================================================= */
/* Panel Device Configuration                                                */
/* ========================================================================= */
typedef struct {
    int reset_gpio_num;
    ili9341_rgb_element_order_t rgb_ele_order;
    uint32_t bits_per_pixel;
    struct {
        uint32_t reset_active_high: 1;
    } flags;
    void *vendor_config;
} ili9341_dev_config_t;

/* ========================================================================= */
/* Panel IO & Panel Interfaces (VTables)                                     */
/* ========================================================================= */
typedef struct ili9341_io_t ili9341_io_t;
struct ili9341_io_t {
    esp_err_t (*rx_param)(ili9341_io_t *io, int lcd_cmd, void *param, size_t param_size);
    esp_err_t (*tx_param)(ili9341_io_t *io, int lcd_cmd, const void *param, size_t param_size);
    esp_err_t (*tx_color)(ili9341_io_t *io, int lcd_cmd, const void *color, size_t color_size);
    esp_err_t (*del)(ili9341_io_t *io);
    esp_err_t (*register_event_callbacks)(ili9341_io_t *io, const ili9341_io_callbacks_t *cbs, void *user_ctx);
};

typedef struct ili9341_panel_t ili9341_panel_t;
struct ili9341_panel_t {
    esp_err_t (*reset)(ili9341_panel_t *panel);
    esp_err_t (*init)(ili9341_panel_t *panel);
    esp_err_t (*del)(ili9341_panel_t *panel);
    esp_err_t (*draw_bitmap)(ili9341_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
    esp_err_t (*mirror)(ili9341_panel_t *panel, bool x_axis, bool y_axis);
    esp_err_t (*swap_xy)(ili9341_panel_t *panel, bool swap_axes);
    esp_err_t (*set_gap)(ili9341_panel_t *panel, int x_gap, int y_gap);
    esp_err_t (*invert_color)(ili9341_panel_t *panel, bool invert_color_data);
    esp_err_t (*disp_on_off)(ili9341_panel_t *panel, bool on_off);
    esp_err_t (*disp_sleep)(ili9341_panel_t *panel, bool sleep);
    void *user_data;
};

/* ========================================================================= */
/* SPI Panel IO Configuration & APIs                                         */
/* ========================================================================= */
typedef int ili9341_spi_bus_handle_t;

typedef struct {
    int cs_gpio_num;
    int dc_gpio_num;
    int spi_mode;
    unsigned int pclk_hz;
    size_t trans_queue_depth;
    ili9341_io_color_trans_done_cb_t on_color_trans_done;
    void *user_ctx;
    int lcd_cmd_bits;
    int lcd_param_bits;
    uint8_t cs_ena_pretrans;
    uint8_t cs_ena_posttrans;
    struct {
        unsigned int dc_high_on_cmd: 1;
        unsigned int dc_low_on_data: 1;
        unsigned int dc_low_on_param: 1;
        unsigned int octal_mode: 1;
        unsigned int quad_mode: 1;
        unsigned int sio_mode: 1;
        unsigned int lsb_first: 1;
        unsigned int cs_high_active: 1;
    } flags;
} ili9341_io_spi_config_t;

esp_err_t ili9341_new_io_spi(ili9341_spi_bus_handle_t bus, const ili9341_io_spi_config_t *io_config, ili9341_io_handle_t *ret_io);

/* ========================================================================= */
/* Generic Panel IO Operations                                               */
/* ========================================================================= */
esp_err_t ili9341_io_rx_param(ili9341_io_handle_t io, int lcd_cmd, void *param, size_t param_size);
esp_err_t ili9341_io_tx_param(ili9341_io_handle_t io, int lcd_cmd, const void *param, size_t param_size);
esp_err_t ili9341_io_tx_color(ili9341_io_handle_t io, int lcd_cmd, const void *color, size_t color_size);
esp_err_t ili9341_io_del(ili9341_io_handle_t io);
esp_err_t ili9341_io_register_event_callbacks(ili9341_io_handle_t io, const ili9341_io_callbacks_t *cbs, void *user_ctx);

/* ========================================================================= */
/* Generic Panel Control Operations                                          */
/* ========================================================================= */
esp_err_t ili9341_panel_reset(ili9341_panel_handle_t panel);
esp_err_t ili9341_panel_init(ili9341_panel_handle_t panel);
esp_err_t ili9341_panel_del(ili9341_panel_handle_t panel);
esp_err_t ili9341_panel_draw_bitmap(ili9341_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
esp_err_t ili9341_panel_mirror(ili9341_panel_handle_t panel, bool mirror_x, bool mirror_y);
esp_err_t ili9341_panel_swap_xy(ili9341_panel_handle_t panel, bool swap_axes);
esp_err_t ili9341_panel_set_gap(ili9341_panel_handle_t panel, int x_gap, int y_gap);
esp_err_t ili9341_panel_invert_color(ili9341_panel_handle_t panel, bool invert_color_data);
esp_err_t ili9341_panel_disp_on_off(ili9341_panel_handle_t panel, bool on_off);
esp_err_t ili9341_panel_disp_sleep(ili9341_panel_handle_t panel, bool sleep);

/* ========================================================================= */
/* ILI9341 Vendor Config & Constructor                                       */
/* ========================================================================= */
typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} ili9341_lcd_init_cmd_t;

typedef struct {
    const ili9341_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
} ili9341_vendor_config_t;

esp_err_t ili9341_new_panel(const ili9341_io_handle_t io, const ili9341_dev_config_t *panel_dev_config, ili9341_panel_handle_t *ret_panel);

#define ILI9341_PANEL_BUS_SPI_CONFIG(sclk, mosi, max_trans_sz)  \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .mosi_io_num = mosi,                                    \
        .miso_io_num = -1,                                      \
        .quadhd_io_num = -1,                                    \
        .quadwp_io_num = -1,                                    \
        .max_transfer_sz = max_trans_sz,                        \
    }

#define ILI9341_PANEL_IO_SPI_CONFIG(cs, dc, callback, callback_ctx) \
    {                                                               \
        .cs_gpio_num = cs,                                          \
        .dc_gpio_num = dc,                                          \
        .spi_mode = 0,                                              \
        .pclk_hz = 40 * 1000 * 1000,                                \
        .trans_queue_depth = 10,                                    \
        .on_color_trans_done = callback,                            \
        .user_ctx = callback_ctx,                                   \
        .lcd_cmd_bits = 8,                                          \
        .lcd_param_bits = 8,                                        \
    }

/* ========================================================================= */
/* ILI9341 Init Command Presets                                              */
/* ========================================================================= */
static const ili9341_lcd_init_cmd_t ili9341_lcd_init_vendor_preset_1[] = {
    /* Power control B */
    {0xCF, (uint8_t []){0x00, 0xC1, 0x30}, 3, 0},
    /* Power on sequence control */
    {0xED, (uint8_t []){0x64, 0x03, 0x12, 0x81}, 4, 0},
    /* Driver timing control A */
    {0xE8, (uint8_t []){0x85, 0x00, 0x78}, 3, 0},
    /* Power control A */
    {0xCB, (uint8_t []){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    /* Pump ratio control */
    {0xF7, (uint8_t []){0x20}, 1, 0},
    /* Driver timing control */
    {0xEA, (uint8_t []){0x00, 0x00}, 2, 0},
    /* Power control 1 */
    {0xC0, (uint8_t []){0x10}, 1, 0},
    /* Power control 2 */
    {0xC1, (uint8_t []){0x00}, 1, 0},
    /* VCOM control 1 */
    {0xC5, (uint8_t []){0x30, 0x30}, 2, 0},
    /* VCOM control 2 */
    {0xC7, (uint8_t []){0xB7}, 1, 0},
    /* Frame rate control */
    {0xB1, (uint8_t []){0x00, 0x1A}, 2, 0},
    /* Enable 3G */
    {0xF2, (uint8_t []){0x00}, 1, 0},
    /* Gamma set */
    {0x26, (uint8_t []){0x01}, 1, 0},
    /* Positive gamma correction */
    {0xE0, (uint8_t []){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15, 0},
    /* Negative gamma correction */
    {0xE1, (uint8_t []){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15, 0},
    /* Entry mode set */
    {0xB7, (uint8_t []){0x07}, 1, 0},
    /* Display function control */
    {0xB6, (uint8_t []){0x08, 0x82, 0x27}, 3, 0},
};

static const ili9341_lcd_init_cmd_t ili9341_lcd_init_vendor_preset_2[] = {
    {0xC8, (uint8_t []){0xFF, 0x93, 0x42}, 3, 0},
    {0xC0, (uint8_t []){0x0E, 0x0E}, 2, 0},
    {0xC5, (uint8_t []){0xD0}, 1, 0},
    {0xC1, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0x00, 0x03, 0x08, 0x06, 0x13, 0x09, 0x39, 0x39, 0x48, 0x02, 0x0a, 0x08, 0x17, 0x17, 0x0F}, 15, 0},
    {0xE1, (uint8_t []){0x00, 0x28, 0x29, 0x01, 0x0d, 0x03, 0x3f, 0x33, 0x52, 0x04, 0x0f, 0x0e, 0x37, 0x38, 0x0F}, 15, 0},
    {0xB1, (uint8_t []){00, 0x1B}, 2, 0},
    {0x36, (uint8_t []){0x08}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0xB7, (uint8_t []){0x06}, 1, 0},
    {0x11, (uint8_t []){0}, 0x80, 0},
    {0x29, (uint8_t []){0}, 0x80, 0},
    {0, (uint8_t []){0}, 0xff, 0},
};

#define ili9341_lcd_init_vendor ili9341_lcd_init_vendor_preset_1

#ifdef __cplusplus
}
#endif