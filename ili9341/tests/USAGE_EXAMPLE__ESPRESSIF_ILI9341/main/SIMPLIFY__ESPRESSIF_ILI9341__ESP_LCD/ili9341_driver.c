/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_LCD_ENABLE_DEBUG_LOG
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "hal/gpio_ll.h"
#include "esp_log.h"
#include "esp_check.h"

#include "ili9341_driver.h"

/* ========================================================================= */
/* 1. Platform Resource Manager                                              */
/* ========================================================================= */
#if SOC_LCDCAM_SUPPORTED
#include "hal/lcd_ll.h"
#include "hal/lcd_hal.h"

typedef struct ili9341_platform_t {
    portMUX_TYPE spinlock;
    union {
        void *panels[SOC_LCDCAM_RGB_NUM_PANELS];
        void *buses[SOC_LCDCAM_I80_NUM_BUSES];
    };
} ili9341_platform_t;

static ili9341_platform_t s_ili9341_platform = {
    .spinlock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED,
    .buses = {}
};

int ili9341_com_register_device(ili9341_com_device_type_t device_type, void *device_obj)
{
    int member_id = -1;
    switch (device_type) {
    case ILI9341_COM_DEVICE_TYPE_I80:
        for (int i = 0; (i < SOC_LCDCAM_I80_NUM_BUSES) && (member_id == -1); i++) {
            portENTER_CRITICAL(&s_ili9341_platform.spinlock);
            if (!s_ili9341_platform.buses[i]) {
                s_ili9341_platform.buses[i] = device_obj;
                member_id = i;
            }
            portEXIT_CRITICAL(&s_ili9341_platform.spinlock);
        }
        break;
    case ILI9341_COM_DEVICE_TYPE_RGB:
        for (int i = 0; (i < SOC_LCDCAM_RGB_NUM_PANELS) && (member_id == -1); i++) {
            portENTER_CRITICAL(&s_ili9341_platform.spinlock);
            if (!s_ili9341_platform.panels[i]) {
                s_ili9341_platform.panels[i] = device_obj;
                member_id = i;
            }
            portEXIT_CRITICAL(&s_ili9341_platform.spinlock);
        }
        break;
    default:
        break;
    }
    return member_id;
}

void ili9341_com_remove_device(ili9341_com_device_type_t device_type, int member_id)
{
    switch (device_type) {
    case ILI9341_COM_DEVICE_TYPE_I80:
        portENTER_CRITICAL(&s_ili9341_platform.spinlock);
        if (s_ili9341_platform.buses[member_id]) {
            s_ili9341_platform.buses[member_id] = NULL;
        }
        portEXIT_CRITICAL(&s_ili9341_platform.spinlock);
        break;
    case ILI9341_COM_DEVICE_TYPE_RGB:
        portENTER_CRITICAL(&s_ili9341_platform.spinlock);
        if (s_ili9341_platform.panels[member_id]) {
            s_ili9341_platform.panels[member_id] = NULL;
        }
        portEXIT_CRITICAL(&s_ili9341_platform.spinlock);
        break;
    default:
        break;
    }
}
#endif // SOC_LCDCAM_SUPPORTED


/* ========================================================================= */
/* 2. Generic Panel IO API Dispatchers                                       */
/* ========================================================================= */
static const char *TAG_IO = "ili9341.io";

esp_err_t ili9341_io_rx_param(ili9341_io_handle_t io, int lcd_cmd, void *param, size_t param_size)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG_IO, "invalid panel io handle");
    ESP_RETURN_ON_FALSE(io->rx_param, ESP_ERR_NOT_SUPPORTED, TAG_IO, "rx_param is not supported yet");
    return io->rx_param(io, lcd_cmd, param, param_size);
}

esp_err_t ili9341_io_tx_param(ili9341_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG_IO, "invalid panel io handle");
    return io->tx_param(io, lcd_cmd, param, param_size);
}

esp_err_t ili9341_io_tx_color(ili9341_io_handle_t io, int lcd_cmd, const void *color, size_t color_size)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG_IO, "invalid panel io handle");
    return io->tx_color(io, lcd_cmd, color, color_size);
}

esp_err_t ili9341_io_del(ili9341_io_handle_t io)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG_IO, "invalid panel io handle");
    return io->del(io);
}

esp_err_t ili9341_io_register_event_callbacks(ili9341_io_handle_t io, const ili9341_io_callbacks_t *cbs, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG_IO, "invalid panel io handle");
    ESP_RETURN_ON_FALSE(cbs, ESP_ERR_INVALID_ARG, TAG_IO, "invalid callbacks structure");
    return io->register_event_callbacks(io, cbs, user_ctx);
}


/* ========================================================================= */
/* 3. SPI Panel IO Transport Engine                                          */
/* ========================================================================= */
static const char *TAG_SPI = "ili9341.io.spi";

static esp_err_t io_spi_rx_param(ili9341_io_t *io, int lcd_cmd, void *param, size_t param_size);
static esp_err_t io_spi_tx_param(ili9341_io_t *io, int lcd_cmd, const void *param, size_t param_size);
static esp_err_t io_spi_tx_color(ili9341_io_t *io, int lcd_cmd, const void *color, size_t color_size);
static esp_err_t io_spi_del(ili9341_io_t *io);
static void spi_pre_trans_cb(spi_transaction_t *trans);
static void spi_post_trans_color_cb(spi_transaction_t *trans);
static esp_err_t io_spi_register_event_callbacks(ili9341_io_handle_t io, const ili9341_io_callbacks_t *cbs, void *user_ctx);

typedef struct {
    spi_transaction_t base;
    struct {
        unsigned int dc_gpio_level: 1;
        unsigned int en_trans_done_cb: 1;
    } flags;
} ili9341_spi_trans_descriptor_t;

typedef struct {
    ili9341_io_t base;            // Base class of generic lcd panel io
    spi_device_handle_t spi_dev;  // SPI device handle
    size_t spi_trans_max_bytes;   // Maximum bytes that can be transmitted in one spi transaction
    int dc_gpio_num;              // D/C line GPIO number
    ili9341_io_color_trans_done_cb_t on_color_trans_done; // User register's callback, invoked when color data trans done
    void *user_ctx;               // User's private data, passed directly to callback on_color_trans_done
    size_t queue_size;            // Size of transaction queue
    size_t num_trans_inflight;    // Number of transactions that are undergoing (the descriptor not recycled yet)
    int lcd_cmd_bits;             // Bit width of LCD command
    int lcd_param_bits;           // Bit width of LCD parameter
    uint8_t cs_ena_pretrans;      // Amount of SPI bit-cycles the cs should be activated before the transmission (0-16)
    uint8_t cs_ena_posttrans;     // Amount of SPI bit-cycles the cs should stay active after the transmission (0-16)
    struct {
        unsigned int dc_cmd_level: 1;    // Indicates the level of DC line when transferring command
        unsigned int dc_data_level: 1;   // Indicates the level of DC line when transferring color data
        unsigned int dc_param_level: 1;  // Indicates the level of DC line when transferring parameters
        unsigned int octal_mode: 1;      // Indicates whether the transmitting is enabled with octal mode (8 data lines)
        unsigned int quad_mode: 1;       // Indicates whether the transmitting is enabled with quad mode (4 data lines)
    } flags;
    ili9341_spi_trans_descriptor_t trans_pool[]; // Transaction pool
} ili9341_io_spi_t;

esp_err_t ili9341_new_io_spi(ili9341_spi_bus_handle_t bus, const ili9341_io_spi_config_t *io_config, ili9341_io_handle_t *ret_io)
{
#if CONFIG_LCD_ENABLE_DEBUG_LOG
    esp_log_level_set(TAG_SPI, ESP_LOG_DEBUG);
#endif
    esp_err_t ret = ESP_OK;
    ili9341_io_spi_t *spi_panel_io = NULL;
    ESP_GOTO_ON_FALSE(bus && io_config && ret_io, ESP_ERR_INVALID_ARG, err, TAG_SPI, "invalid argument");
    spi_panel_io = calloc(1, sizeof(ili9341_io_spi_t) + sizeof(ili9341_spi_trans_descriptor_t) * io_config->trans_queue_depth);
    ESP_GOTO_ON_FALSE(spi_panel_io, ESP_ERR_NO_MEM, err, TAG_SPI, "no mem for spi panel io");

    spi_device_interface_config_t devcfg = {
        .flags = SPI_DEVICE_HALFDUPLEX |
        (io_config->flags.lsb_first ? SPI_DEVICE_TXBIT_LSBFIRST : 0) |
        (io_config->flags.sio_mode ? SPI_DEVICE_3WIRE : 0) |
        (io_config->flags.cs_high_active ? SPI_DEVICE_POSITIVE_CS : 0),
        .clock_speed_hz = io_config->pclk_hz,
        .mode = io_config->spi_mode,
        .spics_io_num = io_config->cs_gpio_num,
        .queue_size = io_config->trans_queue_depth,
        .pre_cb = spi_pre_trans_cb,
        .post_cb = spi_post_trans_color_cb,
        .cs_ena_pretrans = io_config->cs_ena_pretrans,
        .cs_ena_posttrans = io_config->cs_ena_posttrans,
    };
    ret = spi_bus_add_device((spi_host_device_t)bus, &devcfg, &spi_panel_io->spi_dev);
    ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "adding spi device to bus failed");

    if (io_config->dc_gpio_num >= 0) {
        gpio_set_level(io_config->dc_gpio_num, 0);
        gpio_config_t dc_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << io_config->dc_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&dc_conf), err, TAG_SPI, "configure GPIO for DC line failed");
    }

    spi_panel_io->flags.dc_cmd_level = io_config->flags.dc_high_on_cmd;
    spi_panel_io->flags.dc_data_level = !io_config->flags.dc_low_on_data;
    spi_panel_io->flags.dc_param_level = !io_config->flags.dc_low_on_param;
    spi_panel_io->flags.octal_mode = io_config->flags.octal_mode;
    spi_panel_io->flags.quad_mode = io_config->flags.quad_mode;
    spi_panel_io->on_color_trans_done = io_config->on_color_trans_done;
    spi_panel_io->user_ctx = io_config->user_ctx;
    spi_panel_io->lcd_cmd_bits = io_config->lcd_cmd_bits;
    spi_panel_io->lcd_param_bits = io_config->lcd_param_bits;
    spi_panel_io->dc_gpio_num = io_config->dc_gpio_num;
    spi_panel_io->queue_size = io_config->trans_queue_depth;
    spi_panel_io->base.rx_param = io_spi_rx_param;
    spi_panel_io->base.tx_param = io_spi_tx_param;
    spi_panel_io->base.tx_color = io_spi_tx_color;
    spi_panel_io->base.del = io_spi_del;
    spi_panel_io->base.register_event_callbacks = io_spi_register_event_callbacks;

    size_t max_trans_bytes = 0;
    ESP_GOTO_ON_ERROR(spi_bus_get_max_transaction_len((spi_host_device_t)bus, &max_trans_bytes), err, TAG_SPI, "get spi max transaction len failed");
    spi_panel_io->spi_trans_max_bytes = max_trans_bytes;

    *ret_io = &(spi_panel_io->base);
    ESP_LOGD(TAG_SPI, "new spi lcd panel io @%p, max_trans_bytes: %d", spi_panel_io, (int)max_trans_bytes);

    return ESP_OK;

err:
    if (spi_panel_io) {
        if (io_config->dc_gpio_num >= 0) {
            gpio_reset_pin(io_config->dc_gpio_num);
        }
        free(spi_panel_io);
    }
    return ret;
}

static esp_err_t io_spi_del(ili9341_io_t *io)
{
    esp_err_t ret = ESP_OK;
    spi_transaction_t *spi_trans = NULL;
    ili9341_io_spi_t *spi_panel_io = __containerof(io, ili9341_io_spi_t, base);

    size_t num_trans_inflight = spi_panel_io->num_trans_inflight;
    for (size_t i = 0; i < num_trans_inflight; i++) {
        ret = spi_device_get_trans_result(spi_panel_io->spi_dev, &spi_trans, portMAX_DELAY);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "recycle spi transactions failed");
        spi_panel_io->num_trans_inflight--;
    }
    spi_bus_remove_device(spi_panel_io->spi_dev);
    if (spi_panel_io->dc_gpio_num >= 0) {
        gpio_reset_pin(spi_panel_io->dc_gpio_num);
    }
    ESP_LOGD(TAG_SPI, "del lcd panel io spi @%p", spi_panel_io);
    free(spi_panel_io);

err:
    return ret;
}

static esp_err_t io_spi_register_event_callbacks(ili9341_io_handle_t io, const ili9341_io_callbacks_t *cbs, void *user_ctx)
{
    ili9341_io_spi_t *spi_panel_io = __containerof(io, ili9341_io_spi_t, base);

    if (spi_panel_io->on_color_trans_done != NULL) {
        ESP_LOGW(TAG_SPI, "Callback on_color_trans_done was already set and now it was overwritten!");
    }

    spi_panel_io->on_color_trans_done = cbs->on_color_trans_done;
    spi_panel_io->user_ctx = user_ctx;

    return ESP_OK;
}

static void spi_prepare_cmd_buffer(ili9341_io_spi_t *panel_io, const void *cmd)
{
    uint8_t *from = (uint8_t *)cmd;
    if (panel_io->lcd_cmd_bits > 8) {
        int start = 0;
        int end = panel_io->lcd_cmd_bits / 8 - 1;
        ili9341_com_reverse_buffer_bytes(from, start, end);
    }
}

static void spi_prepare_param_buffer(ili9341_io_spi_t *panel_io, const void *param, size_t param_size)
{
    uint8_t *from = (uint8_t *)param;
    int param_width = panel_io->lcd_param_bits / 8;
    size_t param_num = param_size / param_width;
    if (panel_io->lcd_param_bits > 8) {
        for (size_t i = 0; i < param_num; i++) {
            int start = i * param_width;
            int end = start + param_width - 1;
            ili9341_com_reverse_buffer_bytes(from, start, end);
        }
    }
}

static esp_err_t io_spi_tx_param(ili9341_io_t *io, int lcd_cmd, const void *param, size_t param_size)
{
    esp_err_t ret = ESP_OK;
    spi_transaction_t *spi_trans = NULL;
    ili9341_spi_trans_descriptor_t *lcd_trans = NULL;
    ili9341_io_spi_t *spi_panel_io = __containerof(io, ili9341_io_spi_t, base);
    bool send_cmd = (lcd_cmd >= 0);

    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(spi_panel_io->spi_dev, portMAX_DELAY), TAG_SPI, "acquire spi bus failed");

    size_t num_trans_inflight = spi_panel_io->num_trans_inflight;
    for (size_t i = 0; i < num_trans_inflight; i++) {
        ret = spi_device_get_trans_result(spi_panel_io->spi_dev, &spi_trans, portMAX_DELAY);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "recycle spi transactions failed");
        spi_panel_io->num_trans_inflight--;
    }
    lcd_trans = &spi_panel_io->trans_pool[0];
    memset(lcd_trans, 0, sizeof(ili9341_spi_trans_descriptor_t));

    lcd_trans->base.user = spi_panel_io;
    if (param && param_size) {
        lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
    if (spi_panel_io->flags.octal_mode) {
        lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
    }

    if (send_cmd) {
        spi_prepare_cmd_buffer(spi_panel_io, &lcd_cmd);
        lcd_trans->flags.dc_gpio_level = spi_panel_io->flags.dc_cmd_level;
        lcd_trans->base.length = spi_panel_io->lcd_cmd_bits;
        lcd_trans->base.tx_buffer = &lcd_cmd;
        ret = spi_device_polling_transmit(spi_panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "spi transmit (polling) command failed");
    }

    if (param && param_size) {
        spi_prepare_param_buffer(spi_panel_io, param, param_size);
        lcd_trans->flags.dc_gpio_level = spi_panel_io->flags.dc_param_level;
        lcd_trans->base.length = param_size * 8;
        lcd_trans->base.tx_buffer = param;
        lcd_trans->base.flags &= ~SPI_TRANS_CS_KEEP_ACTIVE;
        ret = spi_device_polling_transmit(spi_panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "spi transmit (polling) param failed");
    }

err:
    spi_device_release_bus(spi_panel_io->spi_dev);
    return ret;
}

static esp_err_t io_spi_rx_param(ili9341_io_t *io, int lcd_cmd, void *param, size_t param_size)
{
    esp_err_t ret = ESP_OK;
    spi_transaction_t *spi_trans = NULL;
    ili9341_spi_trans_descriptor_t *lcd_trans = NULL;
    ili9341_io_spi_t *spi_panel_io = __containerof(io, ili9341_io_spi_t, base);
    bool send_cmd = (lcd_cmd >= 0);

    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(spi_panel_io->spi_dev, portMAX_DELAY), TAG_SPI, "acquire spi bus failed");

    size_t num_trans_inflight = spi_panel_io->num_trans_inflight;
    for (size_t i = 0; i < num_trans_inflight; i++) {
        ret = spi_device_get_trans_result(spi_panel_io->spi_dev, &spi_trans, portMAX_DELAY);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "recycle spi transactions failed");
        spi_panel_io->num_trans_inflight--;
    }
    lcd_trans = &spi_panel_io->trans_pool[0];
    memset(lcd_trans, 0, sizeof(ili9341_spi_trans_descriptor_t));

    lcd_trans->base.user = spi_panel_io;
    lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    if (spi_panel_io->flags.octal_mode) {
        lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
    }

    if (send_cmd) {
        spi_prepare_cmd_buffer(spi_panel_io, &lcd_cmd);
        lcd_trans->flags.dc_gpio_level = spi_panel_io->flags.dc_cmd_level;
        lcd_trans->base.length = spi_panel_io->lcd_cmd_bits;
        lcd_trans->base.tx_buffer = &lcd_cmd;
        ret = spi_device_polling_transmit(spi_panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "spi transmit (polling) command failed");
    }

    if (param && param_size) {
        lcd_trans->flags.dc_gpio_level = spi_panel_io->flags.dc_param_level;
        lcd_trans->base.length = 0;
        lcd_trans->base.tx_buffer = NULL;
        lcd_trans->base.rxlength = param_size * 8;
        lcd_trans->base.rx_buffer = param;
        lcd_trans->base.flags &= ~SPI_TRANS_CS_KEEP_ACTIVE;
        ret = spi_device_polling_transmit(spi_panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "spi transmit (polling) param failed");
    }

err:
    spi_device_release_bus(spi_panel_io->spi_dev);
    return ret;
}

static esp_err_t io_spi_tx_color(ili9341_io_t *io, int lcd_cmd, const void *color, size_t color_size)
{
    esp_err_t ret = ESP_OK;
    spi_transaction_t *spi_trans = NULL;
    ili9341_spi_trans_descriptor_t *lcd_trans = NULL;
    ili9341_io_spi_t *spi_panel_io = __containerof(io, ili9341_io_spi_t, base);

    ESP_RETURN_ON_ERROR(spi_device_acquire_bus(spi_panel_io->spi_dev, portMAX_DELAY), TAG_SPI, "acquire spi bus failed");

    bool send_cmd = (lcd_cmd >= 0);
    if (send_cmd) {
        size_t num_trans_inflight = spi_panel_io->num_trans_inflight;
        for (size_t i = 0; i < num_trans_inflight; i++) {
            ret = spi_device_get_trans_result(spi_panel_io->spi_dev, &spi_trans, portMAX_DELAY);
            ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "recycle spi transactions failed");
            spi_panel_io->num_trans_inflight--;
        }
        lcd_trans = &spi_panel_io->trans_pool[0];
        memset(lcd_trans, 0, sizeof(ili9341_spi_trans_descriptor_t));

        spi_prepare_cmd_buffer(spi_panel_io, &lcd_cmd);
        lcd_trans->base.user = spi_panel_io;
        lcd_trans->flags.dc_gpio_level = spi_panel_io->flags.dc_cmd_level;
        lcd_trans->base.length = spi_panel_io->lcd_cmd_bits;
        lcd_trans->base.tx_buffer = &lcd_cmd;
        if (color && color_size) {
            lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
        }
        if (spi_panel_io->flags.octal_mode) {
            lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
        }
        ret = spi_device_polling_transmit(spi_panel_io->spi_dev, &lcd_trans->base);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "spi transmit (polling) command failed");
    }

    do {
        size_t chunk_size = color_size;

        if (spi_panel_io->num_trans_inflight < spi_panel_io->queue_size) {
            lcd_trans = &spi_panel_io->trans_pool[spi_panel_io->num_trans_inflight];
        } else {
            ret = spi_device_get_trans_result(spi_panel_io->spi_dev, &spi_trans, portMAX_DELAY);
            ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "recycle spi transactions failed");
            lcd_trans = __containerof(spi_trans, ili9341_spi_trans_descriptor_t, base);
            spi_panel_io->num_trans_inflight--;
        }
        memset(lcd_trans, 0, sizeof(ili9341_spi_trans_descriptor_t));

        if (chunk_size > spi_panel_io->spi_trans_max_bytes) {
            chunk_size = spi_panel_io->spi_trans_max_bytes;
            lcd_trans->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
        } else {
            lcd_trans->flags.en_trans_done_cb = 1;
            lcd_trans->base.flags &= ~SPI_TRANS_CS_KEEP_ACTIVE;
        }

        lcd_trans->base.user = spi_panel_io;
        lcd_trans->flags.dc_gpio_level = spi_panel_io->flags.dc_data_level;
        lcd_trans->base.length = chunk_size * 8;
        lcd_trans->base.tx_buffer = color;
        if (spi_panel_io->flags.octal_mode) {
            lcd_trans->base.flags |= (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR | SPI_TRANS_MODE_OCT);
        } else if (spi_panel_io->flags.quad_mode) {
            lcd_trans->base.flags |= SPI_TRANS_MODE_QIO;
        }

        ret = spi_device_queue_trans(spi_panel_io->spi_dev, &lcd_trans->base, portMAX_DELAY);
        ESP_GOTO_ON_ERROR(ret, err, TAG_SPI, "spi transmit (queue) color failed");
        spi_panel_io->num_trans_inflight++;

        color = (const uint8_t *)color + chunk_size;
        color_size -= chunk_size;
    } while (color_size > 0);

err:
    spi_device_release_bus(spi_panel_io->spi_dev);
    return ret;
}

IRAM_ATTR static void spi_pre_trans_cb(spi_transaction_t *trans)
{
    ili9341_io_spi_t *spi_panel_io = trans->user;
    ili9341_spi_trans_descriptor_t *lcd_trans = __containerof(trans, ili9341_spi_trans_descriptor_t, base);
    if (spi_panel_io->dc_gpio_num >= 0) {
        gpio_ll_set_level(&GPIO, spi_panel_io->dc_gpio_num, lcd_trans->flags.dc_gpio_level);
        gpio_ll_output_enable(&GPIO, spi_panel_io->dc_gpio_num);
    }
}

static void spi_post_trans_color_cb(spi_transaction_t *trans)
{
    ili9341_io_spi_t *spi_panel_io = trans->user;
    ili9341_spi_trans_descriptor_t *lcd_trans = __containerof(trans, ili9341_spi_trans_descriptor_t, base);

    if (spi_panel_io->dc_gpio_num >= 0) {
        gpio_ll_output_disable(&GPIO, spi_panel_io->dc_gpio_num);
    }

    if (lcd_trans->flags.en_trans_done_cb) {
        if (spi_panel_io->on_color_trans_done) {
            spi_panel_io->on_color_trans_done(&spi_panel_io->base, NULL, spi_panel_io->user_ctx);
        }
    }
}


/* ========================================================================= */
/* 4. Generic Panel Operations Dispatcher                                    */
/* ========================================================================= */
static const char *TAG_OPS = "ili9341.panel";

esp_err_t ili9341_panel_reset(ili9341_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE(panel->reset, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "reset is not supported by this panel");
    return panel->reset(panel);
}

esp_err_t ili9341_panel_init(ili9341_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    return panel->init(panel);
}

esp_err_t ili9341_panel_del(ili9341_panel_handle_t panel)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    return panel->del(panel);
}

esp_err_t ili9341_panel_draw_bitmap(ili9341_panel_handle_t panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE((x_start < x_end) && (y_start < y_end), ESP_ERR_INVALID_ARG, TAG_OPS, "start position must be smaller than end position");
    ESP_RETURN_ON_FALSE(panel->draw_bitmap, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "draw_bitmap is not supported by this panel");
    return panel->draw_bitmap(panel, x_start, y_start, x_end, y_end, color_data);
}

esp_err_t ili9341_panel_mirror(ili9341_panel_handle_t panel, bool mirror_x, bool mirror_y)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE(panel->mirror, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "mirror is not supported by this panel");
    return panel->mirror(panel, mirror_x, mirror_y);
}

esp_err_t ili9341_panel_swap_xy(ili9341_panel_handle_t panel, bool swap_axes)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE(panel->swap_xy, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "swap_xy is not supported by this panel");
    return panel->swap_xy(panel, swap_axes);
}

esp_err_t ili9341_panel_set_gap(ili9341_panel_handle_t panel, int x_gap, int y_gap)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE(panel->set_gap, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "set_gap is not supported by this panel");
    return panel->set_gap(panel, x_gap, y_gap);
}

esp_err_t ili9341_panel_invert_color(ili9341_panel_handle_t panel, bool invert_color_data)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE(panel->invert_color, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "invert_color is not supported by this panel");
    return panel->invert_color(panel, invert_color_data);
}

esp_err_t ili9341_panel_disp_on_off(ili9341_panel_handle_t panel, bool on_off)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE(panel->disp_on_off, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "disp_on_off is not supported by this panel");
    return panel->disp_on_off(panel, on_off);
}

esp_err_t ili9341_panel_disp_sleep(ili9341_panel_handle_t panel, bool sleep)
{
    ESP_RETURN_ON_FALSE(panel, ESP_ERR_INVALID_ARG, TAG_OPS, "invalid panel handle");
    ESP_RETURN_ON_FALSE(panel->disp_sleep, ESP_ERR_NOT_SUPPORTED, TAG_OPS, "sleep is not supported by this panel");
    return panel->disp_sleep(panel, sleep);
}


/* ========================================================================= */
/* 5. ILI9341 Driver Implementation                                          */
/* ========================================================================= */
static const char *TAG_ILI = "ili9341";

static esp_err_t panel_del(ili9341_panel_t *panel);
static esp_err_t panel_reset(ili9341_panel_t *panel);
static esp_err_t panel_init(ili9341_panel_t *panel);
static esp_err_t panel_draw_bitmap(ili9341_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_invert_color(ili9341_panel_t *panel, bool invert_color_data);
static esp_err_t panel_mirror(ili9341_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_swap_xy(ili9341_panel_t *panel, bool swap_axes);
static esp_err_t panel_set_gap(ili9341_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_disp_on_off(ili9341_panel_t *panel, bool off);

typedef struct {
    ili9341_panel_t base;
    ili9341_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_val;
    const ili9341_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
} ili9341_dev_t;

static const ili9341_lcd_init_cmd_t vendor_specific_init_default[] = {
    {0xCF, (uint8_t []){0x00, 0xAA, 0XE0}, 3, 0},
    {0xED, (uint8_t []){0x67, 0x03, 0X12, 0X81}, 4, 0},
    {0xE8, (uint8_t []){0x8A, 0x01, 0x78}, 3, 0},
    {0xCB, (uint8_t []){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t []){0x20}, 1, 0},
    {0xF7, (uint8_t []){0x20}, 1, 0},
    {0xEA, (uint8_t []){0x00, 0x00}, 2, 0},
    {0xC0, (uint8_t []){0x23}, 1, 0},
    {0xC1, (uint8_t []){0x11}, 1, 0},
    {0xC5, (uint8_t []){0x43, 0x4C}, 2, 0},
    {0xC7, (uint8_t []){0xA0}, 1, 0},
    {0xB1, (uint8_t []){0x00, 0x1B}, 2, 0},
    {0xF2, (uint8_t []){0x00}, 1, 0},
    {0x26, (uint8_t []){0x01}, 1, 0},
    {0xE0, (uint8_t []){0x1F, 0x36, 0x36, 0x3A, 0x0C, 0x05, 0x4F, 0X87, 0x3C, 0x08, 0x11, 0x35, 0x19, 0x13, 0x00}, 15, 0},
    {0xE1, (uint8_t []){0x00, 0x09, 0x09, 0x05, 0x13, 0x0A, 0x30, 0x78, 0x43, 0x07, 0x0E, 0x0A, 0x26, 0x2C, 0x1F}, 15, 0},
    {0xB7, (uint8_t []){0x07}, 1, 0},
    {0xB6, (uint8_t []){0x08, 0x82, 0x27}, 3, 0},
};

esp_err_t ili9341_new_panel(const ili9341_io_handle_t io, const ili9341_dev_config_t *panel_dev_config, ili9341_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    ili9341_dev_t *ili9341 = NULL;
    gpio_config_t io_conf = { 0 };

    ESP_GOTO_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, err, TAG_ILI, "invalid argument");
    ili9341 = (ili9341_dev_t *)calloc(1, sizeof(ili9341_dev_t));
    ESP_GOTO_ON_FALSE(ili9341, ESP_ERR_NO_MEM, err, TAG_ILI, "no mem for ili9341 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num;
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG_ILI, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case ILI9341_RGB_ELEMENT_ORDER_RGB:
        ili9341->madctl_val = 0;
        break;
    case ILI9341_RGB_ELEMENT_ORDER_BGR:
        ili9341->madctl_val |= ILI9341_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG_ILI, "unsupported rgb element order");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        ili9341->colmod_val = 0x55;
        ili9341->fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        ili9341->colmod_val = 0x66;
        ili9341->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG_ILI, "unsupported pixel width");
        break;
    }

    ili9341->io = io;
    ili9341->reset_gpio_num = panel_dev_config->reset_gpio_num;
    ili9341->reset_level = panel_dev_config->flags.reset_active_high;
    if (panel_dev_config->vendor_config) {
        ili9341->init_cmds = ((ili9341_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds;
        ili9341->init_cmds_size = ((ili9341_vendor_config_t *)panel_dev_config->vendor_config)->init_cmds_size;
    }
    ili9341->base.del = panel_del;
    ili9341->base.reset = panel_reset;
    ili9341->base.init = panel_init;
    ili9341->base.draw_bitmap = panel_draw_bitmap;
    ili9341->base.invert_color = panel_invert_color;
    ili9341->base.set_gap = panel_set_gap;
    ili9341->base.mirror = panel_mirror;
    ili9341->base.swap_xy = panel_swap_xy;
    ili9341->base.disp_on_off = panel_disp_on_off;
    *ret_panel = &(ili9341->base);
    ESP_LOGD(TAG_ILI, "new ili9341 panel @%p", ili9341);

    ESP_LOGI(TAG_ILI, "LCD panel create success, version: %d.%d.%d", ILI9341_DRV_VER_MAJOR, ILI9341_DRV_VER_MINOR,
             ILI9341_DRV_VER_PATCH);

    return ESP_OK;

err:
    if (ili9341) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(ili9341);
    }
    return ret;
}

static esp_err_t panel_del(ili9341_panel_t *panel)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);

    if (ili9341->reset_gpio_num >= 0) {
        gpio_reset_pin(ili9341->reset_gpio_num);
    }
    ESP_LOGD(TAG_ILI, "del ili9341 panel @%p", ili9341);
    free(ili9341);
    return ESP_OK;
}

static esp_err_t panel_reset(ili9341_panel_t *panel)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    ili9341_io_handle_t io = ili9341->io;

    if (ili9341->reset_gpio_num >= 0) {
        gpio_set_level(ili9341->reset_gpio_num, ili9341->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(ili9341->reset_gpio_num, !ili9341->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_SWRESET, NULL, 0), TAG_ILI, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

static esp_err_t panel_init(ili9341_panel_t *panel)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    ili9341_io_handle_t io = ili9341->io;

    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_SLPOUT, NULL, 0), TAG_ILI, "send command failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_MADCTL, (uint8_t[]) {
        ili9341->madctl_val,
    }, 1), TAG_ILI, "send command failed");
    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_COLMOD, (uint8_t[]) {
        ili9341->colmod_val,
    }, 1), TAG_ILI, "send command failed");

    const ili9341_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    if (ili9341->init_cmds) {
        init_cmds = ili9341->init_cmds;
        init_cmds_size = ili9341->init_cmds_size;
    } else {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(ili9341_lcd_init_cmd_t);
    }

    bool is_cmd_overwritten = false;
    for (int i = 0; i < init_cmds_size; i++) {
        switch (init_cmds[i].cmd) {
        case ILI9341_CMD_MADCTL:
            is_cmd_overwritten = true;
            ili9341->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        case ILI9341_CMD_COLMOD:
            is_cmd_overwritten = true;
            ili9341->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
            break;
        default:
            is_cmd_overwritten = false;
            break;
        }

        if (is_cmd_overwritten) {
            ESP_LOGW(TAG_ILI, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
        }

        ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG_ILI, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }
    ESP_LOGD(TAG_ILI, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_draw_bitmap(ili9341_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    ili9341_io_handle_t io = ili9341->io;

    x_start += ili9341->x_gap;
    x_end   += ili9341->x_gap;
    y_start += ili9341->y_gap;
    y_end   += ili9341->y_gap;

    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4), TAG_ILI, "send command failed");
    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4), TAG_ILI, "send command failed");
    size_t len = (x_end - x_start) * (y_end - y_start) * ili9341->fb_bits_per_pixel / 8;
    ili9341_io_tx_color(io, ILI9341_CMD_RAMWR, color_data, len);

    return ESP_OK;
}

static esp_err_t panel_invert_color(ili9341_panel_t *panel, bool invert_color_data)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    ili9341_io_handle_t io = ili9341->io;
    int command = 0;
    if (invert_color_data) {
        command = ILI9341_CMD_INVON;
    } else {
        command = ILI9341_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, command, NULL, 0), TAG_ILI, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_mirror(ili9341_panel_t *panel, bool mirror_x, bool mirror_y)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    ili9341_io_handle_t io = ili9341->io;
    if (mirror_x) {
        ili9341->madctl_val |= ILI9341_CMD_MX_BIT;
    } else {
        ili9341->madctl_val &= ~ILI9341_CMD_MX_BIT;
    }
    if (mirror_y) {
        ili9341->madctl_val |= ILI9341_CMD_MY_BIT;
    } else {
        ili9341->madctl_val &= ~ILI9341_CMD_MY_BIT;
    }
    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_MADCTL, (uint8_t[]) {
        ili9341->madctl_val
    }, 1), TAG_ILI, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_swap_xy(ili9341_panel_t *panel, bool swap_axes)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    ili9341_io_handle_t io = ili9341->io;
    if (swap_axes) {
        ili9341->madctl_val |= ILI9341_CMD_MV_BIT;
    } else {
        ili9341->madctl_val &= ~ILI9341_CMD_MV_BIT;
    }
    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, ILI9341_CMD_MADCTL, (uint8_t[]) {
        ili9341->madctl_val
    }, 1), TAG_ILI, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_set_gap(ili9341_panel_t *panel, int x_gap, int y_gap)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    ili9341->x_gap = x_gap;
    ili9341->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_disp_on_off(ili9341_panel_t *panel, bool on_off)
{
    ili9341_dev_t *ili9341 = __containerof(panel, ili9341_dev_t, base);
    ili9341_io_handle_t io = ili9341->io;
    int command = on_off ? ILI9341_CMD_DISPON : ILI9341_CMD_DISPOFF;
    ESP_RETURN_ON_ERROR(ili9341_io_tx_param(io, command, NULL, 0), TAG_ILI, "send command failed");
    return ESP_OK;
}