#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "ili9341.hpp"
#include "ili9341_io.hpp"
#include "ili9341_types.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace
{
    // ---- Board wiring: adjust these to match your hardware ----
    constexpr gpio_num_t kPinSclk           = GPIO_NUM_18;
    constexpr gpio_num_t kPinMosi           = GPIO_NUM_23;
    constexpr gpio_num_t kPinCs             = GPIO_NUM_5;
    constexpr gpio_num_t kPinDc             = GPIO_NUM_4;
    constexpr gpio_num_t kPinReset          = GPIO_NUM_13;
    constexpr gpio_num_t kPinBacklight      = GPIO_NUM_27;
    constexpr spi_host_device_t kSpiHost    = SPI2_HOST;
    constexpr int kPclkHz                   = 10 * 1000 * 1000;
    constexpr int kLcdWidth                 = 240;
    constexpr int kLcdHeight                = 320;
    constexpr size_t kSpiQueueDepth         = 10; // must match devCfg.queue_size in Esp32Hal::Init()
    const char *kTag                        = "[ILI9341_EXAMPLE]";
}

// Converts an esp_err_t into the driver's platform-independent status type.
static ILI9341::Status ToStatus(esp_err_t err)
{
    return (err == ESP_OK) ? ILI9341::Status::Ok : ILI9341::Status::ErrorIo;
}

// Concrete Hal implementation backed by ESP-IDF (SPI master + GPIO driver).
//
// SpiWrite() stays blocking/polling, used by SpiIo::TxParam for small
// command/parameter transfers where pipelining has no benefit.
//
// SpiWriteAsync()/SpiWaitIdle() override the Hal defaults to use
// spi_device_queue_trans() instead, so SpiIo::TxColor() can pipeline large
// framebuffer transfers across multiple in-flight transactions instead of
// blocking on each chunk in turn.
class Esp32Hal : public ILI9341::Hal
{
public:
    Esp32Hal() = default;

    ~Esp32Hal() override
    {
        if (spiDev_ != nullptr)
        {
            SpiWaitIdle();
            spi_bus_remove_device(spiDev_);
            spi_bus_free(kSpiHost);
        }
        delete[] transPool_;
    }

    esp_err_t Init()
    {
        spi_bus_config_t busCfg = {};
        busCfg.sclk_io_num   = kPinSclk;
        busCfg.mosi_io_num   = kPinMosi;
        busCfg.miso_io_num   = -1;
        busCfg.quadwp_io_num = -1;
        busCfg.quadhd_io_num = -1;
        // Sized for one full RGB565 line-buffer transfer; SpiIo chunks larger
        // draws against Hal::GetMaxTransferSize(), so this does not need to
        // cover a full frame.
        busCfg.max_transfer_sz = kLcdWidth * 2;

        esp_err_t err = spi_bus_initialize(kSpiHost, &busCfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK)
        {
            return err;
        }

        spi_device_interface_config_t devCfg = {};
        devCfg.clock_speed_hz = kPclkHz;
        devCfg.mode = 0;
        devCfg.spics_io_num = kPinCs;
        devCfg.queue_size = kSpiQueueDepth;

        err = spi_bus_add_device(kSpiHost, &devCfg, &spiDev_);
        if (err != ESP_OK)
        {
            spi_bus_free(kSpiHost);
            return err;
        }

        size_t maxLen = 0;
        err = spi_bus_get_max_transaction_len(kSpiHost, &maxLen);
        if (err != ESP_OK)
        {
            spi_bus_remove_device(spiDev_);
            spiDev_ = nullptr;
            spi_bus_free(kSpiHost);
            return err;
        }
        maxTransferSize_ = maxLen;

        transPool_ = new spi_transaction_t[kSpiQueueDepth]();

        return ESP_OK;
    }

    void DelayMs(uint32_t ms) override
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    void SetGpioLevel(int gpio, bool level) override
    {
        gpio_set_level(static_cast<gpio_num_t>(gpio), level ? 1 : 0);
    }

    void ReleasePin(int gpio) override
    {
        gpio_reset_pin(static_cast<gpio_num_t>(gpio));
    }

    void ConfigureOutputPin(int gpio) override
    {
        gpio_config_t cfg = {};
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pin_bit_mask = 1ULL << gpio;
        gpio_config(&cfg);
    }

    ILI9341::Status SpiWrite(const uint8_t *data, size_t len) override
    {
        if (len == 0)
        {
            return ILI9341::Status::Ok;
        }
        spi_transaction_t trans = {};
        trans.length = len * 8;
        trans.tx_buffer = data;
        esp_err_t err = spi_device_polling_transmit(spiDev_, &trans);
        return ToStatus(err);
    }

    // Queues the chunk and returns immediately. If the queue is full, this
    // blocks only long enough to reclaim one completed slot -- not until the
    // new transaction itself finishes -- so several chunks can be in flight
    // at once while TxColor() prepares/queues the next one.
    ILI9341::Status SpiWriteAsync(const uint8_t *data, size_t len) override
    {
        if (len == 0)
        {
            return ILI9341::Status::Ok;
        }

        if (inflight_ >= kSpiQueueDepth)
        {
            spi_transaction_t *done = nullptr;
            esp_err_t err = spi_device_get_trans_result(spiDev_, &done, portMAX_DELAY);
            if (err != ESP_OK)
            {
                return ToStatus(err);
            }
            inflight_--;
        }

        spi_transaction_t *t = &transPool_[nextSlot_];
        memset(t, 0, sizeof(*t));
        t->length = len * 8;
        t->tx_buffer = data;

        esp_err_t err = spi_device_queue_trans(spiDev_, t, portMAX_DELAY);
        if (err != ESP_OK)
        {
            return ToStatus(err);
        }
        inflight_++;
        nextSlot_ = (nextSlot_ + 1) % kSpiQueueDepth;
        return ILI9341::Status::Ok;
    }

    // Drains every transaction still in flight. SpiIo::TxColor() calls this
    // once after the chunk loop, and also on error, before it is safe to
    // change the DC pin level or start a different SPI transaction.
    ILI9341::Status SpiWaitIdle() override
    {
        while (inflight_ > 0)
        {
            spi_transaction_t *done = nullptr;
            esp_err_t err = spi_device_get_trans_result(spiDev_, &done, portMAX_DELAY);
            if (err != ESP_OK)
            {
                return ToStatus(err);
            }
            inflight_--;
        }
        return ILI9341::Status::Ok;
    }

    size_t GetMaxTransferSize() override
    {
        return maxTransferSize_;
    }

private:
    spi_device_handle_t spiDev_ = nullptr;
    size_t maxTransferSize_ = 4092;
    spi_transaction_t *transPool_ = nullptr;
    size_t nextSlot_ = 0;
    size_t inflight_ = 0;
};

// ---------------------------------------------------------------------------
// Minimal graphics primitives built on top of Panel::DrawBitmap. Panel does
// not expose SetPixel/DrawLine/etc, so everything here is composed from
// windowed pixel blits.
// ---------------------------------------------------------------------------

static inline uint16_t Rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Solid-fill rectangle. Reuses a single row buffer, so w is capped to kLcdWidth.
static ILI9341::Status FillRect(ILI9341::Panel &lcd, int x, int y, int w, int h, uint16_t color)
{
    static uint16_t rowBuf[kLcdWidth];
    int clampedW = (w > kLcdWidth) ? kLcdWidth : w;
    for (int i = 0; i < clampedW; i++)
    {
        rowBuf[i] = color;
    }

    ILI9341::Status s = ILI9341::Status::Ok;
    for (int row = 0; row < h; row++)
    {
        s = lcd.DrawBitmap(x, y + row, x + clampedW, y + row + 1, rowBuf);
        if (s != ILI9341::Status::Ok)
        {
            return s;
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// In-memory pixel buffer primitives. These write into a plain array (no SPI
// traffic at all), so they are used to compose a scene locally, then the
// whole buffer is blitted to the panel in one DrawBitmap call.
//
// A full-screen buffer (240*320*2 = 150 KB) is deliberately avoided here:
// this driver targets multiple MCUs, including STM32 targets with far less
// RAM than an ESP32. A buffer sized to just the animated region keeps this
// demo portable instead of assuming ESP32-class memory.
// ---------------------------------------------------------------------------

static inline void SetPixelBuf(uint16_t *buf, int bufW, int bufH, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= bufW || y >= bufH)
    {
        return;
    }
    buf[y * bufW + x] = color;
}

// Bresenham line, drawn into a local buffer instead of over SPI.
static void DrawLineBuf(uint16_t *buf, int bufW, int bufH, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = std::abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        SetPixelBuf(buf, bufW, bufH, x0, y0, color);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

// Midpoint circle, outline only, drawn into a local buffer.
static void DrawCircleBuf(uint16_t *buf, int bufW, int bufH, int cx, int cy, int r, uint16_t color)
{
    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        SetPixelBuf(buf, bufW, bufH, cx + x, cy + y, color);
        SetPixelBuf(buf, bufW, bufH, cx + y, cy + x, color);
        SetPixelBuf(buf, bufW, bufH, cx - y, cy + x, color);
        SetPixelBuf(buf, bufW, bufH, cx - x, cy + y, color);
        SetPixelBuf(buf, bufW, bufH, cx - x, cy - y, color);
        SetPixelBuf(buf, bufW, bufH, cx - y, cy - x, color);
        SetPixelBuf(buf, bufW, bufH, cx + y, cy - x, color);
        SetPixelBuf(buf, bufW, bufH, cx + x, cy - y, color);

        y += 1;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0)
        {
            x -= 1;
            err += 1 - 2 * x;
        }
    }
}

// ---------------------------------------------------------------------------
// Demo scenes
// ---------------------------------------------------------------------------

// Vertical blue-to-red gradient, drawn one row at a time.
static void DrawGradientBackground(ILI9341::Panel &lcd)
{
    static uint16_t rowBuf[kLcdWidth];
    for (int y = 0; y < kLcdHeight; y++)
    {
        uint8_t r = static_cast<uint8_t>(255 * y / kLcdHeight);
        uint8_t b = static_cast<uint8_t>(255 - r);
        uint16_t color = Rgb565(r, 0, b);
        for (int x = 0; x < kLcdWidth; x++)
        {
            rowBuf[x] = color;
        }
        lcd.DrawBitmap(0, y, kLcdWidth, y + 1, rowBuf);
    }
}

// Row of color swatches near the top, useful for checking RGB order / gamma.
static void DrawColorSwatches(ILI9341::Panel &lcd)
{
    constexpr uint16_t kColors[] = {
        0xF800, // red
        0x07E0, // green
        0x001F, // blue
        0xFFE0, // yellow
        0xF81F, // magenta
        0x07FF, // cyan
        0xFFFF, // white
        0x0000, // black
    };
    constexpr int kCount = sizeof(kColors) / sizeof(kColors[0]);
    constexpr int kSwatchW = kLcdWidth / kCount;
    constexpr int kSwatchH = 24;

    for (int i = 0; i < kCount; i++)
    {
        FillRect(lcd, i * kSwatchW, 8, kSwatchW, kSwatchH, kColors[i]);
    }
}

// Analog clock face with a sweeping hand, animated for a fixed number of
// frames. The face is pre-rendered once into a template buffer; each frame
// copies the template, draws the hand on top, and blits the whole region in
// a single DrawBitmap call -- one SPI transaction sequence per frame instead
// of one per pixel.
static void DrawAnimatedClock(ILI9341::Panel &lcd, int frames)
{
    constexpr int kBufSize = 160; // covers radius 70 + hand with margin
    constexpr int kRadius  = 70;
    constexpr uint16_t kFaceColor = 0xFFFF; // white
    constexpr uint16_t kHandColor = 0xF800; // red
    constexpr uint16_t kBgColor   = 0x0000; // black

    const int screenCx = kLcdWidth / 2;
    const int screenCy = 200;
    const int bufX0 = screenCx - kBufSize / 2;
    const int bufY0 = screenCy - kBufSize / 2;
    const int localCx = kBufSize / 2;
    const int localCy = kBufSize / 2;

    static uint16_t frameBuf[kBufSize * kBufSize];

    for (int i = 0; i < frames; i++)
    {
        for (int p = 0; p < kBufSize * kBufSize; p++)
        {
            frameBuf[p] = kBgColor;
        }
        DrawCircleBuf(frameBuf, kBufSize, kBufSize, localCx, localCy, kRadius, kFaceColor);

        float angle = (2.0f * static_cast<float>(M_PI) * i) / frames - static_cast<float>(M_PI) / 2.0f;
        int hx = localCx + static_cast<int>(kRadius * 0.8f * std::cos(angle));
        int hy = localCy + static_cast<int>(kRadius * 0.8f * std::sin(angle));

        DrawLineBuf(frameBuf, kBufSize, kBufSize, localCx, localCy, hx, hy, kHandColor);

        lcd.DrawBitmap(bufX0, bufY0, bufX0 + kBufSize, bufY0 + kBufSize, frameBuf);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

extern "C" void app_main()
{
    // 1. Bring up the SPI bus + device on the ESP32 side.
    static Esp32Hal hal;
    esp_err_t err = hal.Init();
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "Esp32Hal::Init failed: %d", err);
        return;
    }

    // 2. GPIO lines the driver does not own directly: D/C (used by SpiIo)
    //    and Reset (used by Panel::Reset). Both must be configured as
    //    outputs here, before SpiIo / Panel touch them, or SetGpioLevel()
    //    calls on them are no-ops against an unconfigured pin.
    hal.ConfigureOutputPin(kPinBacklight);
    hal.SetGpioLevel(kPinBacklight, true);
    hal.ConfigureOutputPin(kPinDc);
    hal.ConfigureOutputPin(kPinReset);

    ILI9341::SpiIoConfig ioConfig;
    ioConfig.dcGpio = kPinDc;
    ioConfig.maxChunkBytes = 0; // ask Hal::GetMaxTransferSize()
    ioConfig.flags.dcCmdLevel = 0;
    ioConfig.flags.dcDataLevel = 0;
    ioConfig.flags.dcParamLevel = 0;

    static ILI9341::SpiIo io(hal, ioConfig);

    // 3. Panel setup.
    ILI9341::Config panelConfig;
    panelConfig.resetGpioNum = kPinReset;
    panelConfig.rgbOrder = ILI9341::RgbElementOrder::Rgb;
    panelConfig.bitsPerPixel = ILI9341::BitsPerPixel::Bpp16;
    panelConfig.flags.resetActiveHigh = 0;

    static ILI9341::Panel lcd(io, hal, panelConfig);

    ILI9341::Status s = lcd.Reset();
    if (s != ILI9341::Status::Ok)
    {
        ESP_LOGE(kTag, "Panel::Reset failed: %d", static_cast<int>(s));
        return;
    }

    s = lcd.Init();
    if (s != ILI9341::Status::Ok)
    {
        ESP_LOGE(kTag, "Panel::Init failed: %d", static_cast<int>(s));
        return;
    }

    lcd.InvertColor(false);
    lcd.SetGap(0, 0);
    lcd.DispOnOff(true);

    // 4. Scene sequence: gradient background -> color swatches -> animated
    //    clock hand. Gradient and clock frames are full DrawBitmap calls,
    //    so they exercise the SpiWriteAsync/SpiWaitIdle path in SpiIo::TxColor
    //    (multiple chunks in flight per call instead of one blocking SpiWrite
    //    per chunk). Color swatches exercise many small windowed blits.
    ESP_LOGI(kTag, "Drawing gradient background...");
    DrawGradientBackground(lcd);

    ESP_LOGI(kTag, "Drawing color swatches...");
    DrawColorSwatches(lcd);

    ESP_LOGI(kTag, "Animating clock hand...");
    DrawAnimatedClock(lcd, 24);

    ESP_LOGI(kTag, "Demo done.");
}