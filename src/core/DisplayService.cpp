#include "DisplayService.h"

#if HAS_TFT

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>
#include <driver/gpio.h>

#include "../descriptors.h"
#include "SettingsPrefs.h"
#include "SolarState.h"

extern Settings _settings;

#ifndef TFT_ROTATION
#define TFT_ROTATION 1
#endif
// T-Display buttons: GPIO35 (right of USB, input only, external pull-up) and GPIO0 (left, BOOT).
#ifndef TFT_BUTTON_NEXT
#define TFT_BUTTON_NEXT 35
#endif
#ifndef TFT_BUTTON_PREV
#define TFT_BUTTON_PREV 0
#endif
// One screen with everything instead of pages you tap through. The paged layout is a 240x135
// design scaled up, which looks soft on a big panel; the dashboard draws at native font size.
#ifndef TFT_DASH_PAGES
#define TFT_DASH_PAGES 0
#endif
#define TFT_DASH_PAGE_COUNT 4
#define TFT_DASH_FLOW_PAGE 1    // Power flow: the default screen, and the one on the dashboard's palette
#ifndef DISPLAY_DEMO
#define DISPLAY_DEMO 0
#endif

namespace
{
#if TFT_BOARD_CROWPANEL_ADV35
// Elecrow CrowPanel Advance 3.5" (ESP32-S3, 480x320 IPS): ILI9488 on SPI2, GT911 capacitive touch on I2C.
// Pins from Elecrow's LovyanGFX_Driver.h and the Meshtastic variant (see docs-alt-boards.md).
class LGFX_Board : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_GT911 _touch;

public:
    LGFX_Board()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 42;
            cfg.pin_mosi = 39;
            cfg.pin_miso = -1;
            cfg.pin_dc = 41;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 40;
            cfg.pin_rst = 2; // also the wireless-slot reset; use -1 if a wireless module is fitted
            cfg.pin_busy = -1;
            cfg.memory_width = 320;
            cfg.memory_height = 480;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 3; // landscape 480x320 at rotation 0
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 38; // active high
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = 319;
            cfg.y_min = 0;
            cfg.y_max = 479;
            cfg.pin_int = 47;
            cfg.pin_rst = 48;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = I2C_NUM_0;
            cfg.pin_sda = 15;
            cfg.pin_scl = 16;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x14; // LovyanGFX falls back to 0x5D automatically
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
#elif TFT_BOARD_CYD35
// Sunton/JC ESP32-3248S035 ("3.5 inch ESP32 LCD TFT Module" on AliExpress): ST7796 320x480 on HSPI, backlight 27.
// R version: XPT2046 resistive touch on the shared SPI bus (CS 33). C version (-DCYD35_TOUCH_CAP=1): GT911 on I2C 33/32.
// Pins and calibration from docs-cyd35.md (rzeldent, macsbug, LovyanGFX #811, factory demos).
class LGFX_Board : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
#if CYD35_TOUCH_CAP
    lgfx::Touch_GT911 _touch;
#else
    lgfx::Touch_XPT2046 _touch;
#endif

public:
    LGFX_Board()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_dc = 2;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 15;
            cfg.pin_rst = -1; // tied to EN
            cfg.pin_busy = -1;
            cfg.memory_width = 320;
            cfg.memory_height = 480;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 27; // active high
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
#if CYD35_TOUCH_CAP
            cfg.x_min = 0;
            cfg.x_max = 319;
            cfg.y_min = 0;
            cfg.y_max = 479;
            cfg.pin_int = -1; // INT is not wired on shipped boards
            cfg.pin_rst = 25;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port = 1;
            cfg.i2c_addr = 0x5D; // LovyanGFX falls back to 0x14
            cfg.pin_sda = 33;
            cfg.pin_scl = 32;
            cfg.freq = 400000;
#else
            cfg.x_min = 360;
            cfg.x_max = 4200;
            cfg.y_min = 180;
            cfg.y_max = 3900;
            cfg.pin_int = -1; // GPIO36 IRQ is unreliable at 240 MHz; poll instead
            cfg.bus_shared = true;
            cfg.offset_rotation = 3; // per macsbug with display rotation 1; verify on hardware
            cfg.spi_host = SPI2_HOST;
            cfg.freq = 1000000;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_cs = 33;
#endif
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
#elif TFT_BOARD_CROWPANEL35
// Elecrow CrowPanel ESP32 3.5" (DIS05035H): ILI9488 320x480 on HSPI, XPT2046 resistive touch on the same bus.
// Pins verified against Elecrow's V2.0/V2.2 schematics (docs-crowpanel35.md). V2.2 is the default; define
// CROWPANEL35_HW_V20 for V1.x/V2.0/V2.1 boards (TFT MISO and touch CS are swapped there).
// The ILI9488 SDO is not wired, so the MISO pin only carries the touch controller's data.
#ifdef CROWPANEL35_HW_V20
#define CP35_PIN_MISO 12
#define CP35_PIN_TOUCH_CS 33
#else
#define CP35_PIN_MISO 33
#define CP35_PIN_TOUCH_CS 12
#endif
#define CP35_PIN_SCLK 14
#define CP35_PIN_MOSI 13
#define CP35_PIN_DC 2
#define CP35_PIN_CS 15
#define CP35_PIN_BL 27       // active high, off at reset
#define CP35_PIN_TOUCH_IRQ 36

// LovyanGFX's stock ILI9488 MADCTL table mirrors this panel; use TFT_eSPI's table so rotation 1 = landscape,
// USB-C on the right, exactly like Elecrow's own examples.
class Panel_ILI9488_CrowPanel : public lgfx::Panel_ILI9488
{
protected:
    uint8_t getMadCtl(uint8_t r) const override
    {
        static constexpr uint8_t t[] = {
            MAD_MX | MAD_MH, MAD_MV, MAD_MY | MAD_ML, MAD_MV | MAD_MX | MAD_MY | MAD_MH | MAD_ML,
            MAD_MX | MAD_MY | MAD_MH | MAD_ML, MAD_MV | MAD_MX | MAD_MH, 0, MAD_MV | MAD_MY | MAD_ML};
        return t[r & 7];
    }
};

class LGFX_Board : public lgfx::LGFX_Device
{
    Panel_ILI9488_CrowPanel _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX_Board()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI2_HOST; // HSPI on the classic ESP32
            cfg.spi_mode = 0;
            cfg.freq_write = 27000000; // Elecrow's IDF examples use 27 MHz; 40 MHz is unverified
            cfg.freq_read = 6000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = CP35_PIN_SCLK;
            cfg.pin_mosi = CP35_PIN_MOSI;
            cfg.pin_miso = CP35_PIN_MISO;
            cfg.pin_dc = CP35_PIN_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = CP35_PIN_CS;
            cfg.pin_rst = -1; // panel reset is tied to the ESP32 EN line
            cfg.pin_busy = -1;
            cfg.memory_width = 320;
            cfg.memory_height = 480;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false; // SDO not wired
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = CP35_PIN_BL;
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            // Elecrow's ESPHome calibration for V2.2, in the panel's rotation-0 frame (y inverted).
            cfg.x_min = 280;
            cfg.x_max = 3860;
            cfg.y_min = 3860;
            cfg.y_max = 340;
            cfg.pin_int = CP35_PIN_TOUCH_IRQ;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host = SPI2_HOST;
            cfg.freq = 1000000;
            cfg.pin_sclk = CP35_PIN_SCLK;
            cfg.pin_mosi = CP35_PIN_MOSI;
            cfg.pin_miso = CP35_PIN_MISO;
            cfg.pin_cs = CP35_PIN_TOUCH_CS;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
#else
// LilyGO / TTGO T-Display: ST7789V 135x240 on VSPI, 3-wire SPI (SDA is bidirectional, no MISO).
class LGFX_Board : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Light_PWM _light;

public:
    LGFX_Board()
    {
        {
            auto cfg = _bus.config();
            cfg.spi_host = SPI3_HOST; // VSPI on the classic ESP32
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 6000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 18;
            cfg.pin_mosi = 19;
            cfg.pin_miso = -1;
            cfg.pin_dc = 16;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = 5;
            cfg.pin_rst = 23;
            cfg.pin_busy = -1;
            cfg.panel_width = 135;
            cfg.panel_height = 240;
            cfg.offset_x = 52;
            cfg.offset_y = 40;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = 4;
            cfg.invert = false;
            cfg.freq = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};
#endif

// Reference layout (T-Display, 240x135); everything is scaled from these at runtime.
constexpr int kBaseW = 240;
constexpr int kBaseH = 135;
constexpr int kBaseHeaderY = 2;
constexpr int kBaseBigTop = 22;
constexpr int kBaseBigBottom = 97;
constexpr int kBaseRowAY = 101;
constexpr int kBaseRowBY = 119;

// Live data access. liveData is filled from the main loop, and this service also runs there.
bool readNumber(const char *key, float &out)
{
    if (liveData.isNull())
    {
        return false;
    }
    JsonVariant value = liveData[key];
    if (value.isNull())
    {
        return false;
    }
    if (value.is<const char *>())
    {
        const char *text = value.as<const char *>();
        if (text == nullptr || *text == '\0')
        {
            return false;
        }
    }
    out = value.as<float>();
    return true;
}

String readText(const char *key)
{
    if (liveData.isNull())
    {
        return String();
    }
    JsonVariant value = liveData[key];
    if (value.isNull())
    {
        return String();
    }
    return value.as<String>();
}

String fmt(float value, uint8_t decimals)
{
    return String(value, static_cast<unsigned int>(decimals));
}

String fmtOrDash(const char *key, uint8_t decimals, const char *unit)
{
    float value = 0;
    if (!readNumber(key, value))
    {
        return String("--") + unit;
    }
    return fmt(value, decimals) + unit;
}

uint32_t levelColor(int percent)
{
    if (percent < 0)
    {
        return TFT_DARKGREY;
    }
    if (percent >= 50)
    {
        return TFT_GREEN;
    }
    if (percent >= 20)
    {
        return TFT_YELLOW;
    }
    return TFT_RED;
}

// Signed battery current in amps (+ charging, - discharging), if the inverter reports it.
bool readBatteryCurrent(float &out)
{
    float load = 0;
    if (readNumber(DESCR_Battery_Load, load))
    {
        out = load;
        return true;
    }
    float charge = 0;
    float discharge = 0;
    const bool haveCharge = readNumber(DESCR_Battery_Charge_Current, charge);
    const bool haveDischarge = readNumber(DESCR_Battery_Discharge_Current, discharge);
    if (!haveCharge && !haveDischarge)
    {
        return false;
    }
    out = charge - discharge;
    return true;
}
} // namespace

struct DisplayService::Impl
{
    LGFX_Board tft;
    int lastRenderedPage = -1;
    uint32_t bg = TFT_BLACK; // page background; the flow page uses the dashboard's #08131D
    bool lastTallFont = true;
    float lastBigTextSize = 0.0f;

    // Geometry derived from the real panel size.
    int W = kBaseW;
    int H = kBaseH;
    float sx = 1.0f;   // horizontal scale vs. the reference layout
    float sy = 1.0f;   // vertical scale
    float fs = 1.0f;   // integer font scale (bitmap fonts stay crisp)
    int headerY = kBaseHeaderY;
    int bigTop = kBaseBigTop;
    int bigBottom = kBaseBigBottom;
    int rowAY = kBaseRowAY;
    int rowBY = kBaseRowBY;
    // Single-screen geometry, in real pixels (no reference-layout scaling).
    int padX = 8;
    int gap = 8;
    int hdrH = 40;
    int footTop = 280;
    int colX = 240;

    void computeGeometry()
    {
        W = tft.width();
        H = tft.height();
        sx = static_cast<float>(W) / kBaseW;
        sy = static_cast<float>(H) / kBaseH;
        fs = floorf(sx < sy ? sx : sy);
        if (fs < 1.0f) fs = 1.0f;
        headerY = static_cast<int>(kBaseHeaderY * sy);
        bigTop = static_cast<int>(kBaseBigTop * sy);
        bigBottom = static_cast<int>(kBaseBigBottom * sy);
        rowAY = static_cast<int>(kBaseRowAY * sy);
        rowBY = static_cast<int>(kBaseRowBY * sy);
        padX = W / 40;
        gap = W / 60;
        hdrH = H * 7 / 50;
        footTop = H - H * 7 / 50;
        colX = W / 2;
    }
    int X(int baseX) const { return static_cast<int>(baseX * sx); }

    // No off-screen buffer: every element is drawn with background padding so updates do not flicker
    // and the 32 KB sprite stays available as heap for TLS and the web server.
    lgfx::LGFXBase &target() { return tft; }

    void text(const String &s, int x, int y, textdatum_t datum, int padding, uint32_t color)
    {
        lgfx::LGFXBase &g = target();
        g.setTextDatum(datum);
        g.setTextPadding(padding);
        g.setTextSize(fs);
        g.setTextColor(color, bg);
        g.drawString(s, x, y);
        g.setTextPadding(0);
    }

    // Same, but at the font's own size: no integer scaling, so glyph edges stay sharp.
    void at(const String &s, int x, int y, textdatum_t datum, int padding, uint32_t color)
    {
        lgfx::LGFXBase &g = target();
        g.setTextDatum(datum);
        g.setTextPadding(padding);
        g.setTextSize(1);
        g.setTextColor(color, bg);
        g.drawString(s, x, y);
        g.setTextPadding(0);
    }

    void drawBigValue(const String &value, const String &unit, uint32_t color)
    {
        lgfx::LGFXBase &g = target();
        // Font8 (75 px) for short values, Font7 (48 px, seven segment) for long ones.
        const bool tall = value.length() <= 3;
        if (tall != lastTallFont)
        {
            g.fillRect(0, bigTop, W, bigBottom - bigTop, TFT_BLACK);
            lastTallFont = tall;
        }
        g.setFont(tall ? &fonts::Font8 : &fonts::Font7);
        g.setTextSize(fs);
        const int valueHeight = g.fontHeight();
        const int top = bigTop + (bigBottom - bigTop - valueHeight) / 2;
        const int split = X(172);
        text(value, split - X(4), top, textdatum_t::top_right, split - X(4), color);
        g.setFont(&fonts::Font4);
        text(unit, split + X(2), top + valueHeight, textdatum_t::bottom_left, W - split - X(2), color);
    }

    void drawBigText(const String &value, uint32_t color)
    {
        lgfx::LGFXBase &g = target();
        g.setFont(&fonts::Font4);
        float size = 2.0f * fs;
        g.setTextSize(size);
        if (g.textWidth(value) > W - X(8))
        {
            size = 1.5f * fs;
            g.setTextSize(size);
        }
        if (g.textWidth(value) > W - X(8))
        {
            size = 1.0f * fs;
            g.setTextSize(size);
        }
        if (size != lastBigTextSize)
        {
            g.fillRect(0, bigTop, W, bigBottom - bigTop, TFT_BLACK);
            lastBigTextSize = size;
        }
        g.setTextDatum(textdatum_t::middle_center);
        g.setTextPadding(W - X(4));
        g.setTextColor(color, bg);
        g.drawString(value, W / 2, (bigTop + bigBottom) / 2);
        g.setTextPadding(0);
        g.setTextSize(fs);
    }
};

void DisplayService::begin()
{
    if (_impl != nullptr)
    {
        return;
    }
    _impl = new Impl();
    _impl->tft.init();
    _impl->tft.setRotation(TFT_ROTATION);
#if TFT_BOARD_CYD35 && !CYD35_TOUCH_CAP
    // Measured with calibrateTouch() on the ESP32-3248S035R at rotation 1 (2026-09-24). Without it the
    // raw XPT2046 ranges from the datasheets map every tap into a 50 px strip, so only one edge ever fires.
    static uint16_t kTouchCal[8] = {340, 3867, 3786, 3847, 350, 325, 3801, 297};
    _impl->tft.setTouchCalibrate(kTouchCal);
#endif
    _impl->tft.setBrightness(255); // full: the 3.5" backlight is dim at the old T-Display default of 200
    _impl->tft.fillScreen(TFT_BLACK);
    _impl->computeGeometry();

    _btnNext.pin = TFT_BUTTON_NEXT;
    _btnPrev.pin = TFT_BUTTON_PREV;
    if (_btnNext.pin >= 0)
    {
        pinMode(_btnNext.pin, _btnNext.pin >= 34 ? INPUT : INPUT_PULLUP); // GPIO34..39 have no pull-ups
    }
    if (_btnPrev.pin >= 0)
    {
        pinMode(_btnPrev.pin, _btnPrev.pin >= 34 ? INPUT : INPUT_PULLUP);
    }

#if TFT_DASH_PAGES
    _page = TFT_DASH_FLOW_PAGE; // the board wakes showing the power flow
#else
    _page = PageBattery;
#endif
    render(false, false, false, String());
    _drawnOnce = true;
    _lastDrawMs = millis();
}

// Returns true once per press (falling edge after debounce). Buttons are active low.
bool DisplayService::pollButton(Button &button, uint32_t now)
{
    if (button.pin < 0)
    {
        return false;
    }
    const bool level = digitalRead(button.pin) != LOW;
    if (level != button.lastRead)
    {
        button.lastRead = level;
        button.lastChangeMs = now;
        return false;
    }
    if (level == button.stableLevel || (now - button.lastChangeMs) < kDebounceMs)
    {
        return false;
    }
    button.stableLevel = level;
    return !level;
}


// Touch (when the board has a panel): a tap on the left half is "previous", on the right half "next".
bool DisplayService::pollTouch(uint32_t now, bool &next)
{
#if TFT_TOUCH
    int32_t tx = 0;
    int32_t ty = 0;
    const bool down = _impl->tft.getTouch(&tx, &ty) != 0;
    if (down)
    {
        _touchLastSeenMs = now;
        const bool rising = !_touchWasDown;
        _touchWasDown = true; // latch every contact, accepted or not
        if (rising && (now - _touchDownMs) > kTouchLockoutMs)
        {
            _touchDownMs = now;
            next = tx >= _impl->W / 2;
            return true;
        }
    }
    else if (_touchWasDown && (now - _touchLastSeenMs) > kTouchReleaseMs) // release must be stable
    {
        _touchWasDown = false;
    }
    return false;
#else
    (void)now;
    (void)next;
    return false;
#endif
}

int DisplayService::panelWidth() const { return _impl ? _impl->W : 0; }
int DisplayService::panelHeight() const { return _impl ? _impl->H : 0; }

bool DisplayService::readRow(int y, uint8_t *bgr, int width)
{
    if (_impl == nullptr || bgr == nullptr) { return false; }
    static lgfx::rgb888_t line[520];
    if (width > 520) { width = 520; }
    _impl->tft.readRect(0, y, width, 1, line); // true colour, so the caller needs no pixel-format guesswork
    for (int x = 0; x < width; ++x)
    {
        bgr[x * 3 + 0] = line[x].b;
        bgr[x * 3 + 1] = line[x].g;
        bgr[x * 3 + 2] = line[x].r;
    }
    return true;
}

void DisplayService::requestTouchCalibration()
{
    _calibRequested = true;
}

// The eight values LovyanGFX returns from calibrateTouch(); false until a calibration has run.
bool DisplayService::calibrationValues(uint16_t *out) const
{
    if (out == nullptr || !_calibDone) { return false; }
    for (int i = 0; i < 8; ++i) { out[i] = _calib[i]; }
    return true;
}

void DisplayService::setRedrawHold(bool on)
{
    _holdRedraw = on;
    _holdSinceMs = millis();
    if (!on) { _forceRedraw = true; } // repaint once the reader is done
}

void DisplayService::setLedOverride(int mode, int yellowGreen)
{
    _ledOverride = mode;
    if (yellowGreen >= 0) { _yellowGreen = yellowGreen; }
    _forceRedraw = true; // the LED is refreshed from the render
}

void DisplayService::setPage(uint8_t page)
{
    _page = static_cast<uint8_t>(page % TFT_DASH_PAGE_COUNT);
    _forceRedraw = true;
}

#if TFT_DASH_PAGES
// Called from the main loop and from inside a redraw: a tap that starts and ends while the screen is
// being painted would otherwise fall between two samples and be lost.
void DisplayService::latchTouch(uint32_t now)
{
    bool tapRight = false;
    if (pollTouch(now, tapRight))
    {
        _tapSteps.fetch_add(tapRight ? 1 : -1, std::memory_order_relaxed);
    }
}
#endif

void DisplayService::loop(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress)
{
    if (_impl == nullptr)
    {
        return;
    }
    const uint32_t now = millis();

#if DISPLAY_TOOLS
    if (_calibRequested)
    {
        // LovyanGFX draws a target in each corner and waits for a tap; the eight values it returns map
        // raw touch readings to screen pixels for this panel and rotation. Bake them into the board class.
        _calibRequested = false;
        lgfx::LGFXBase &cg = _impl->target();
        cg.fillScreen(TFT_BLACK);
        cg.setFont(&fonts::FreeSansBold12pt7b);
        cg.setTextDatum(textdatum_t::middle_center);
        cg.setTextColor(TFT_WHITE, TFT_BLACK);
        cg.drawString("Touch calibration", _impl->W / 2, _impl->H / 2 - 20);
        cg.setFont(&fonts::FreeSans9pt7b);
        cg.drawString("tap each corner target", _impl->W / 2, _impl->H / 2 + 12);
        delay(1200);
        _impl->tft.calibrateTouch(_calib, TFT_WHITE, TFT_BLACK, 24);
        _calibDone = true;
        Serial.printf("[TOUCH] calibration %u %u %u %u %u %u %u %u\n", _calib[0], _calib[1], _calib[2], _calib[3],
                      _calib[4], _calib[5], _calib[6], _calib[7]);
        _forceRedraw = true;
        _drawnOnce = false;
    }
#endif
#if TFT_DASH_PAGES
    // Not while a reader is copying the panel: the touch controller sits on the same SPI bus, and
    // polling it from this task while the web task reads rows corrupts the rows still to come.
    // A capture drops taps for its duration; the hold self-releases if the reader dies.
    if (!_holdRedraw) { latchTouch(now); }
    const int steps = _tapSteps.exchange(0, std::memory_order_relaxed);
    if (steps != 0)
    {
        int page = (static_cast<int>(_page) + steps) % TFT_DASH_PAGE_COUNT;
        if (page < 0) { page += TFT_DASH_PAGE_COUNT; }
        _page = static_cast<uint8_t>(page);
        _forceRedraw = true;
    }
#else
    const bool solarConnected = _settings.get.solarConnected();
    auto pageHidden = [solarConnected](uint8_t page) { return page == PageSolar && !solarConnected; };
    bool tapNext = false;
    const bool tapped = pollTouch(now, tapNext);
    if (pollButton(_btnNext, now) || (tapped && tapNext))
    {
        do { _page = static_cast<uint8_t>((_page + 1) % PageCount); } while (pageHidden(_page));
        _forceRedraw = true;
    }
    if (pollButton(_btnPrev, now) || (tapped && !tapNext))
    {
        do { _page = static_cast<uint8_t>((_page + PageCount - 1) % PageCount); } while (pageHidden(_page));
        _forceRedraw = true;
    }
    if (pageHidden(_page))
    {
        _page = PageBattery;
        _forceRedraw = true;
    }
#endif // !TFT_DASH_PAGES

#if TFT_DASH_PAGES
    // The flow dots move far faster than the page redraws, so they get their own cheap pass.
    if (_page == TFT_DASH_FLOW_PAGE && _drawnOnce && !_holdRedraw
        && _impl->lastRenderedPage == TFT_DASH_FLOW_PAGE) { animateFlow(now); }
#endif

    if (_holdRedraw)
    {
        if (now - _holdSinceMs < 15000) { return; } // a reader is copying the panel; never freeze for long
        _holdRedraw = false;
        _forceRedraw = true;
    }
    if (!_forceRedraw && (now - _lastPollMs) < kPollIntervalMs)
    {
        return;
    }
    _lastPollMs = now;

    const String signature = buildSignature(wifiConnected, apMode, inverterConnected, ipAddress);
    const bool changed = signature != _lastSignature;
    if (!_forceRedraw && !changed && (now - _lastDrawMs) < kForceRedrawMs)
    {
        return;
    }

    render(wifiConnected, apMode, inverterConnected, ipAddress);
    _lastSignature = signature;
    _lastDrawMs = now;
    _forceRedraw = false;
    _drawnOnce = true;
}

// Cheap change detector: everything the current page shows, concatenated.
String DisplayService::buildSignature(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress) const
{
    String s;
    s.reserve(160);
    s += _page;
    s += wifiConnected ? 'W' : 'w';
    s += apMode ? 'A' : 'a';
    s += inverterConnected ? 'I' : 'i';
    s += ipAddress;
    s += '|';
#if TFT_DASH_PAGES
    s += _page;
    if (inverterConnected)
    {
        s += readText(DESCR_Inverter_Operation_Mode);
        s += fmtOrDash(DESCR_Battery_Percent, 0, "") + fmtOrDash(DESCR_Battery_Voltage, 1, "") +
             fmtOrDash(DESCR_Battery_Charge_Current, 0, "") + fmtOrDash(DESCR_Battery_Discharge_Current, 0, "") +
             fmtOrDash(DESCR_AC_In_Voltage, 1, "") + fmtOrDash(DESCR_AC_In_Frequency, 1, "") +
             fmtOrDash(DESCR_AC_Out_Watt, 0, "") + fmtOrDash(DESCR_AC_Out_Percent, 0, "") +
             fmtOrDash(DESCR_AC_Out_Voltage, 1, "") + fmtOrDash(DESCR_AC_Out_Frequency, 1, "") +
             fmtOrDash(DESCR_PV_Charging_Power, 0, "") + fmtOrDash(DESCR_Inverter_Bus_Temperature, 0, "");
    }
    return s;
#endif
    if (inverterConnected)
    {
        s += readText(DESCR_Inverter_Operation_Mode);
        s += '|';
        switch (_page)
        {
        case PageBattery:
            s += fmtOrDash(DESCR_Battery_Percent, 0, "") + fmtOrDash(DESCR_Battery_Voltage, 1, "") +
                 fmtOrDash(DESCR_Battery_Load, 0, "") + fmtOrDash(DESCR_Battery_Charge_Current, 0, "") +
                 fmtOrDash(DESCR_Battery_Discharge_Current, 0, "");
            break;
        case PageLoad:
            s += fmtOrDash(DESCR_AC_Out_Watt, 0, "") + fmtOrDash(DESCR_AC_Out_Percent, 0, "") +
                 fmtOrDash(DESCR_AC_Out_Voltage, 1, "");
            break;
        case PageSolar:
            s += fmtOrDash(DESCR_PV_Charging_Power, 0, "") + fmtOrDash(DESCR_PV_Input_Voltage, 1, "") +
                 fmtOrDash(DESCR_PV_Input_Power, 0, "");
            break;
        default:
            s += fmtOrDash(DESCR_AC_In_Voltage, 1, "") + fmtOrDash(DESCR_Battery_Voltage, 1, "") +
                 fmtOrDash(DESCR_Inverter_Bus_Temperature, 0, "") + fmtOrDash(DESCR_PV_Charging_Power, 0, "");
            break;
        }
    }
    return s;
}

#if TFT_DASH_PAGES

// Four screens, tapped left/right: summary, power flow, 24 h history, alerts. The palette and the
// rules are the Telegram dashboard's, so the panel and the phone show the same thing.
namespace
{
constexpr uint32_t kBlue = 0x58B8FF, kGreen = 0x30C977, kAmber = 0xF6C549, kRed = 0xFF8A78;
constexpr uint32_t kMuted = 0x8AA0B5, kInk = 0xEEF6FF, kTrack = 0x2A3A48, kPanel = 0x08131D;

// The flow began as a 1:1 copy of the dashboard's 340x228 SVG, scaled to fit under the header.
// The panel is wider than that box is tall, so a uniform fit left the rings small: at the line
// where the value sits there were only 96 px of clear width for text up to 93 px wide, and a
// reading like "12.4 kW" would not have fitted at all. The layout is now in panel pixels and keeps
// the dashboard's shape - two rings above, one below, S-curved links - with bigger rings spread
// across the full width.
struct FlowRing { float cx, cy, r; };
constexpr FlowRing kFlowRings[3] = {{96, 116, 62}, {384, 116, 62}, {240, 230, 58}};
constexpr float kRingStroke = 5.0f;                  // the SVG's 4, grown with the rings
constexpr float kValueDrop = 18.0f;                  // value sits below centre, as in the SVG
constexpr float kIconRise = 16.0f;

// Endpoints sit on the ring circles; the links are trimmed clear of them so repainting a link
// during the dot animation can never scribble on a ring.
struct FlowPath { bool curved; float p[8]; };
constexpr FlowPath kFlowPaths[3] = {
    {false, {158, 116, 0, 0, 0, 0, 322, 116}},                                 // grid -> home
    {true, {152.6f, 141.2f, 207.6f, 141.2f, 226.0f, 147.7f, 226.0f, 173.7f}},  // grid -> battery
    {true, {254.0f, 173.7f, 254.0f, 147.7f, 272.4f, 141.2f, 327.4f, 141.2f}},  // battery -> home
};

void flowPointAt(int i, float t, float &x, float &y)
{
    const FlowPath &f = kFlowPaths[i];
    if (!f.curved)
    {
        x = f.p[0] + (f.p[6] - f.p[0]) * t;
        y = f.p[1] + (f.p[7] - f.p[1]) * t;
        return;
    }
    const float u = 1.0f - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    x = a * f.p[0] + b * f.p[2] + c * f.p[4] + d * f.p[6];
    y = a * f.p[1] + b * f.p[3] + c * f.p[5] + d * f.p[7];
}

bool nearRing(float x, float y, float margin)
{
    for (int i = 0; i < 3; ++i)
    {
        const float dx = x - kFlowRings[i].cx, dy = y - kFlowRings[i].cy;
        const float keep = kFlowRings[i].r + margin;
        if (dx * dx + dy * dy <= keep * keep) { return true; }
    }
    return false;
}
constexpr float kLinkKeepOut = kRingStroke / 2 + 1.0f;
constexpr float kDotKeepOut = kLinkKeepOut + 7.0f; // also clears the rubber used to wipe a dot

// The dashboard's animateMotion duration: the more power flows, the faster the dot runs.
inline float flowDur(float watts) { return fmaxf(0.8f, 4.0f - watts / 1500.0f); }
constexpr int kPages = TFT_DASH_PAGE_COUNT;

uint32_t rgb(lgfx::LGFXBase &g, uint32_t v)
{
    return g.color888((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// The dashboard's green-to-red scale: hsl(120t, 75%, 52%), t = 1 green, 0 red.
uint32_t scaleColor(lgfx::LGFXBase &g, float t)
{
    if (t < 0) { t = 0; }
    if (t > 1) { t = 1; }
    const float h = 120.0f * t / 60.0f, c = 0.75f * (1.0f - fabsf(2.0f * 0.52f - 1.0f)), m = 0.52f - c / 2.0f;
    const float x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
    float r = 0, gr = 0, b = 0;
    if (h < 1) { r = c; gr = x; }
    else if (h < 2) { r = x; gr = c; }
    else { gr = c; b = x; }
    return g.color888(static_cast<int>((r + m) * 255), static_cast<int>((gr + m) * 255), static_cast<int>((b + m) * 255));
}

String kw(float w)
{
    return w >= 1000.0f ? String(w / 1000.0f, 1) + " kW" : String(static_cast<long>(w + 0.5f)) + " W";
}

String dur(uint32_t s)
{
    const uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if (d > 0) { return String(d) + "d " + String(h) + "H " + String(m) + "m"; }
    if (h > 0) { return String(h) + "H " + String(m) + "m"; }
    return String(m) + "m";
}

// Green on grid, amber on battery, red for a fault, a dead link or a nearly flat battery.
uint32_t stateColor(const DisplayService::Snap &s, String *label);

#if TFT_BOARD_CYD35
// On-board RGB LED beside the screen: R 4, G 17, B 16, active low. Confirmed on the board:
// with green on 16 it glowed blue, so macsbug's pin order is the right one for this unit.
// Both channels run on PWM: the green die is much brighter than the red one, so a flat-out mix
// reads as green rather than yellow.
void setBoardLed(uint32_t state, int mode, int yellowGreen)
{
    static int lastRed = -1, lastGreen = -1;
    static bool ready = false;
    static bool greenPwm = false;
    int red = 0, green = 0, blue = 0; // 0..255 brightness
    switch (mode != 0 ? mode : (state == kGreen ? 1 : (state == kAmber ? 2 : (state == kRed ? 3 : 4))))
    {
    case 1: green = 255; break;
    case 2: red = 255; green = yellowGreen; break;
    case 3: red = 255; break;
    case 5: blue = 255; break;              // for comparing brightness by eye
    case 6: red = 255; blue = 255; break;   // magenta: a brighter stand-in for a weak red
    default: break;
    }
    static int lastBlue = -1;
    if (red == lastRed && green == lastGreen && blue == lastBlue) { return; }
    lastRed = red;
    lastGreen = green;
    lastBlue = blue;
    if (!ready)
    {
        pinMode(4, OUTPUT);
        pinMode(16, OUTPUT);
        pinMode(17, OUTPUT);
        // Ask for the strongest drive on the red pin, in case the dim red is the pin and not a resistor.
        gpio_set_drive_capability(GPIO_NUM_4, GPIO_DRIVE_CAP_3);
        digitalWrite(16, HIGH); // blue off
        ready = true;
    }
    // Full on and full off go through plain pins; only the partial green of the yellow mix needs PWM,
    // and the pin is handed back to GPIO afterwards so a later full-on is really full on.
    digitalWrite(4, red > 0 ? LOW : HIGH);
    digitalWrite(16, blue > 0 ? LOW : HIGH);
    if (green > 0 && green < 255)
    {
        if (!greenPwm)
        {
            ledcAttach(17, 5000, 8);
            greenPwm = true;
        }
        ledcWrite(17, 255 - green); // active low
    }
    else
    {
        if (greenPwm)
        {
            ledcDetach(17);
            pinMode(17, OUTPUT);
            greenPwm = false;
        }
        digitalWrite(17, green > 0 ? LOW : HIGH);
    }
}
#endif

uint32_t modeColor(const String &mode)
{
    if (mode.equalsIgnoreCase("Line")) { return kGreen; }
    if (mode.equalsIgnoreCase("Battery")) { return kAmber; }
    if (mode.equalsIgnoreCase("Fault")) { return kRed; }
    return kBlue;
}

} // namespace

// Everything the four screens draw. Filled from the live values, or generated in a preview build.
struct DisplayService::Snap
{
    bool link = false;
    String mode;
    bool gridOff = false;
    float gridV = 0, gridHz = 0, gridW = 0;
    float battPct = 0, battV = 0, battChargeW = 0, battDischargeW = 0;
    float loadW = 0, loadPct = 0, outV = 0, outHz = 0;
    float pvW = 0, tempC = 0;
    int fullPct = 100, ratingW = 6000;
    uint32_t leftS = 0;
    uint8_t batt[96], load[96], off[96];
    int slots = 0;
    char aType[8];
    uint16_t aValue[8];
    uint32_t aAgeMin[8];
    int alerts = 0;
};

#if DISPLAY_DEMO
void DisplayService::fillDemo(Snap &s)
{
    s.link = true;
    s.mode = "Line";
    s.gridV = 223.7f; s.gridHz = 50.0f; s.gridW = 1640.0f;
    s.battPct = 88; s.battV = 53.4f; s.battChargeW = 400.0f;
    s.loadW = 1240.0f; s.loadPct = 31; s.outV = 230.1f; s.outHz = 50.0f;
    s.tempC = 48; s.fullPct = 90; s.leftS = 37080;
    s.slots = 96;
    for (int i = 0; i < 96; ++i)
    {
        const float ph = i / 95.0f;
        s.batt[i] = static_cast<uint8_t>(62 + 26 * sinf(ph * 3.1f + 1.2f));
        const float lw = 900.0f + 800.0f * sinf(ph * 9.0f) + 300.0f * sinf(ph * 21.0f);
        s.load[i] = static_cast<uint8_t>(fmaxf(0.0f, fminf(240.0f, lw / 25.0f)));
        s.off[i] = (i >= 40 && i <= 46) ? (i == 40 || i == 46 ? 7 : 15) : 0;
    }
    const char t[] = {'G', 'g', 'b', 'p', 'r', 'i'};
    const uint16_t v[] = {0, 0, 25, 55, 0, 3};
    const uint32_t a[] = {143, 218, 260, 611, 1455, 2880};
    for (int i = 0; i < 6; ++i) { s.aType[i] = t[i]; s.aValue[i] = v[i]; s.aAgeMin[i] = a[i]; }
    s.alerts = 6;
}
#endif

void DisplayService::fillLive(Snap &s, bool inverterConnected)
{
    s.link = inverterConnected;
    if (!inverterConnected) { return; }
    s.mode = readText(DESCR_Inverter_Operation_Mode);
    float v = 0;
    if (readNumber(DESCR_AC_In_Voltage, v)) { s.gridV = v; }
    if (readNumber(DESCR_AC_In_Frequency, v)) { s.gridHz = v; }
    s.gridOff = s.gridV < 50.0f;
    if (readNumber(DESCR_Battery_Percent, v)) { s.battPct = v; }
    if (readNumber(DESCR_Battery_Voltage, v)) { s.battV = v; }
    if (readNumber(DESCR_AC_Out_Watt, v)) { s.loadW = v; }
    if (readNumber(DESCR_AC_Out_Percent, v)) { s.loadPct = v; }
    if (readNumber(DESCR_AC_Out_Voltage, v)) { s.outV = v; }
    if (readNumber(DESCR_AC_Out_Frequency, v)) { s.outHz = v; }
    if (readNumber(DESCR_PV_Charging_Power, v)) { s.pvW = v; }
    if (readNumber(DESCR_Inverter_Bus_Temperature, v)) { s.tempC = v; }
    float current = 0;
    s.battChargeW = 0; // zero current must clear these, not leave the previous fill's value standing
    s.battDischargeW = 0;
    if (readBatteryCurrent(current))
    {
        if (current > 0) { s.battChargeW = current * s.battV; }
        else if (current < 0) { s.battDischargeW = -current * s.battV; }
    }
    const float eff = _settings.get.inverterEfficiencyPct() > 0 ? _settings.get.inverterEfficiencyPct() / 100.0f : 1.0f;
    const float idle = _settings.get.inverterIdleW();
    s.gridW = s.gridOff ? 0 : s.loadW + fmaxf(0.0f, s.battChargeW - s.pvW) / eff + idle;
    s.fullPct = _settings.get.batteryFullPct() > 0 ? _settings.get.batteryFullPct() : 100;
    const float wh = _settings.get.batteryCapacityWh();
    const float reserve = _settings.get.batteryReservePct();
    const float draw = s.loadW / eff + idle;
    s.leftS = (wh > 0 && draw > 0 && s.battPct > reserve)
                  ? static_cast<uint32_t>(wh * (s.battPct - reserve) / 100.0f / draw * 3600.0f)
                  : 0;
}

namespace
{
uint32_t stateColor(const DisplayService::Snap &s, String *label)
{
    const bool onBattery = s.link && (s.gridOff || s.mode.equalsIgnoreCase("Battery"));
    if (!s.link)
    {
        if (label != nullptr) { *label = "No inverter"; }
        return kRed;
    }
    if (s.mode.equalsIgnoreCase("Fault"))
    {
        if (label != nullptr) { *label = "Fault"; }
        return kRed;
    }
    if (s.battPct > 0 && s.battPct < 30) // a low battery outranks everything else
    {
        if (label != nullptr) { *label = "Battery low"; }
        return kRed;
    }
    if (onBattery)
    {
        if (label != nullptr) { *label = "On battery"; }
        return kAmber;
    }
    if (label != nullptr) { *label = "On grid"; }
    return kGreen;
}
} // namespace

// Header: page title, the page dots, link letters and the clock.
void DisplayService::drawHeader(const char *title, bool wifiConnected, bool apMode, bool inverterConnected)
{
    Impl &I = *_impl;
    lgfx::LGFXBase &g = I.target();
    g.fillRect(0, 0, I.W, I.hdrH, I.bg); // clear the strip: padding alone leaves tails of longer titles
    const int dotY = I.hdrH / 2, dotR = 4, step = 18, x0 = I.W / 2 - ((kPages - 1) * step) / 2;
    const int titleMax = x0 - dotR - 10 - I.padX;
    g.setFont(&fonts::FreeSansBold18pt7b);
    if (g.textWidth(title) > titleMax) { g.setFont(&fonts::FreeSansBold12pt7b); } // "Power flow" ran into the dots
    I.at(title, I.padX, I.hdrH / 2, textdatum_t::middle_left, titleMax, rgb(g, kInk));

    g.fillRect(I.W / 2 - 40, dotY - 6, 80, 12, I.bg); // page background, or the flow page shows a black band
    for (int i = 0; i < kPages; ++i)
    {
        if (i == _page) { g.fillCircle(x0 + i * step, dotY, dotR, rgb(g, kInk)); }
        else { g.drawCircle(x0 + i * step, dotY, dotR, rgb(g, kTrack)); }
    }

    (void)wifiConnected;
    (void)apMode;
    (void)inverterConnected;
    const time_t now = time(nullptr);
    if (now > 1700000000)
    {
        struct tm tmv;
        const time_t shifted = now + static_cast<time_t>(_settings.get.tzOffsetHours()) * 3600;
        gmtime_r(&shifted, &tmv);
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        I.at(buf, I.W - I.padX, dotY, textdatum_t::middle_right, 90, rgb(g, kMuted));
    }
    g.drawFastHLine(0, I.hdrH, I.W, rgb(g, kTrack));
}

// Page 0: three things only - the battery as a ring with its percentage, the load as a ring with no
// number, and the time left along the bottom. The state colour rides a thin accent, not a heavy band.
void DisplayService::drawStatus(const Snap &s)
{
    Impl &I = *_impl;
    lgfx::LGFXBase &g = I.target();
    const int W = I.W, H = I.H;
    const uint32_t state = stateColor(s, nullptr);

    const int cy = 168, rOut = 96, rIn = 74, lx = 124, rx2 = 356;

    g.fillArc(lx, cy, rIn, rOut, 0, 360, rgb(g, kTrack));
    const float frac = s.fullPct > 0 ? fminf(1.0f, s.battPct / s.fullPct) : 0.0f;
    if (frac > 0.01f) { g.fillArc(lx, cy, rIn, rOut, 270, 270 + static_cast<int>(360 * frac), rgb(g, state)); }
    g.setFont(&fonts::Font8);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(rgb(g, state), TFT_BLACK);
    g.drawString(s.link ? String(static_cast<int>(s.battPct + 0.5f)) : String("--"), lx, cy);

    g.fillArc(rx2, cy, rIn, rOut, 0, 360, rgb(g, kTrack));
    const float loadFrac = fminf(1.0f, s.loadW / (s.ratingW > 0 ? s.ratingW : 6000.0f));
    if (loadFrac > 0.01f) { g.fillArc(rx2, cy, rIn, rOut, 270, 270 + static_cast<int>(360 * loadFrac), rgb(g, state)); }
    g.setFont(&fonts::Font8);
    g.setTextDatum(textdatum_t::middle_center);
    g.setTextColor(rgb(g, state), TFT_BLACK);
    // from the same fraction as the arc, so the number and the ring can never disagree
    g.drawString(s.link ? String(static_cast<int>(loadFrac * 100.0f + 0.5f)) : String("--"), rx2, cy);
    g.setFont(&fonts::FreeSansBold24pt7b);
    I.at(s.leftS ? dur(s.leftS) : String("--"), W / 2, H - 8, textdatum_t::bottom_center, W, rgb(g, state));
}

void DisplayService::drawFlowPaths()
{
    lgfx::LGFXBase &g = _impl->target();
    const uint32_t offCol = rgb(g, kTrack);
    for (int i = 0; i < 3; ++i)
    {
        const uint32_t c = _flowOn[i] ? _flowCol[i] : offCol;
        // A segment is dropped whole, so the step has to be small or the trim leaves a visible gap.
        const int steps = kFlowPaths[i].curved ? 36 : 64;
        float px = 0, py = 0;
        flowPointAt(i, 0.0f, px, py);
        for (int k = 1; k <= steps; ++k)
        {
            float qx = 0, qy = 0;
            flowPointAt(i, static_cast<float>(k) / steps, qx, qy);
            // stroke-width 2 in the SVG; drawWideLine takes the half-width and rounds the ends
            if (!nearRing(px, py, kLinkKeepOut) && !nearRing(qx, qy, kLinkKeepOut))
            {
                g.drawWideLine(static_cast<int>(px), static_cast<int>(py),
                               static_cast<int>(qx), static_cast<int>(qy), 1.2f, c);
            }
            px = qx;
            py = qy;
        }
    }
}

// Runs between full redraws so the dots move without repainting the page: rub out the old dots,
// lay the three links back down, then draw the dots at their new positions.
void DisplayService::animateFlow(uint32_t now)
{
    if (_impl == nullptr || (now - _flowLastMs) < 70) { return; }
    const float dt = (now - _flowLastMs) / 1000.0f;
    _flowLastMs = now;
    if (!_flowOn[0] && !_flowOn[1] && !_flowOn[2]) { return; }

    lgfx::LGFXBase &g = _impl->target();
    const uint32_t bg = rgb(g, kPanel);
    for (int i = 0; i < 3; ++i)
    {
        if (_flowDotX[i] >= 0) { g.fillSmoothCircle(_flowDotX[i], _flowDotY[i], 7, bg); _flowDotX[i] = -1; }
    }
    drawFlowPaths();
    for (int i = 0; i < 3; ++i)
    {
        if (!_flowOn[i]) { continue; }
        _flowPhase[i] += dt / _flowDur[i];
        while (_flowPhase[i] >= 1.0f) { _flowPhase[i] -= 1.0f; }
        float x = 0, y = 0;
        flowPointAt(i, _flowPhase[i], x, y);
        if (nearRing(x, y, kDotKeepOut)) { continue; } // it would be under the ring anyway
        _flowDotX[i] = static_cast<int>(x + 0.5f);
        _flowDotY[i] = static_cast<int>(y + 0.5f);
        g.fillSmoothCircle(_flowDotX[i], _flowDotY[i], 5, _flowCol[i]); // SVG dot r=4, scaled
    }
}

void DisplayService::drawFlow(const Snap &s)
{
    Impl &I = *_impl;
    lgfx::LGFXBase &g = I.target();
    const uint32_t bg = rgb(g, kPanel), track = rgb(g, kTrack), muted = rgb(g, kMuted);
    const float limit = s.ratingW > 0 ? s.ratingW : 6000.0f;
    const uint32_t gridCol = scaleColor(g, 1.0f - (s.gridW - 500.0f) / 4500.0f);
    const uint32_t battCol = scaleColor(g, (s.battPct - 25.0f) / 55.0f);
    const uint32_t homeCol = s.gridOff ? scaleColor(g, 1.0f - (s.loadW - 500.0f) / 4500.0f) : rgb(g, kBlue);

    _flowOn[0] = !s.gridOff && s.loadW > 0;
    _flowOn[1] = s.battChargeW >= 1 && !s.gridOff;
    _flowOn[2] = s.battDischargeW >= 1;
    _flowCol[0] = rgb(g, kBlue);
    _flowCol[1] = rgb(g, kBlue);
    _flowCol[2] = rgb(g, kAmber);
    _flowDur[0] = flowDur(s.loadW);
    _flowDur[1] = flowDur(s.battChargeW);
    _flowDur[2] = flowDur(s.battDischargeW);
    // Rub out any dot still on screen first: redrawing the 2 px link does not cover a 5 px dot,
    // so without this a full repaint leaves the previous dot behind as a ghost.
    for (int i = 0; i < 3; ++i)
    {
        if (_flowDotX[i] >= 0) { g.fillSmoothCircle(_flowDotX[i], _flowDotY[i], 7, bg); }
        _flowDotX[i] = -1;
    }
    drawFlowPaths();

    // A ring is the SVG's grey circle with the value arc stroked over it from 12 o'clock.
    auto ring = [&](int idx, float frac, uint32_t color, bool showArc) {
        const FlowRing &R = kFlowRings[idx];
        const int cx = static_cast<int>(R.cx + 0.5f), cy = static_cast<int>(R.cy + 0.5f);
        const float hw = kRingStroke / 2;
        g.fillSmoothCircle(cx, cy, static_cast<int>(R.r - hw), bg);
        g.fillArc(cx, cy, static_cast<int>(R.r - hw), static_cast<int>(R.r + hw), 0.0f, 360.0f, track);
        if (!showArc || frac <= 0.002f) { return; }
        const float sweep = 360.0f * fminf(1.0f, frac);
        g.fillArc(cx, cy, static_cast<int>(R.r - hw), static_cast<int>(R.r + hw), 270.0f, 270.0f + sweep, color);
        const float ends[2] = {270.0f, 270.0f + sweep}; // stroke-linecap="round"
        for (int e = 0; e < 2; ++e)
        {
            const float rad = ends[e] * 3.14159265f / 180.0f;
            g.fillSmoothCircle(static_cast<int>(R.cx + R.r * cosf(rad) + 0.5f),
                               static_cast<int>(R.cy + R.r * sinf(rad) + 0.5f),
                               static_cast<int>(hw + 0.5f), color);
        }
    };
    ring(0, s.gridOff ? 0.0f : s.gridW / limit, gridCol, !s.gridOff);
    ring(1, s.gridOff ? s.loadW / limit : 1.0f, homeCol, true);
    ring(2, s.fullPct > 0 ? s.battPct / s.fullPct : 0.0f, battCol, true);

    // The dashboard puts an emoji in each ring; the board's fonts are Latin-1, so these are drawn.
    auto icon = [&](int idx) {
        const int cx = static_cast<int>(kFlowRings[idx].cx + 0.5f);
        const int cy = static_cast<int>(kFlowRings[idx].cy - kIconRise + 0.5f);
        if (idx == 0) // lightning
        {
            g.fillTriangle(cx + 4, cy - 11, cx - 7, cy + 4, cx - 1, cy + 4, muted);
            g.fillTriangle(cx - 4, cy + 14, cx + 7, cy - 1, cx + 1, cy - 1, muted);
        }
        else if (idx == 1) // house
        {
            g.fillTriangle(cx, cy - 11, cx - 13, cy + 1, cx + 13, cy + 1, muted);
            g.fillRect(cx - 9, cy + 1, 18, 11, muted);
        }
        else // battery
        {
            g.fillRect(cx - 12, cy - 7, 21, 14, muted);
            g.fillRect(cx + 9, cy - 3, 4, 6, muted);
        }
    };
    icon(0);
    icon(1);
    icon(2);

    // Value inside each ring, with the dashboard's arrow glyph drawn as a triangle.
    auto value = [&](int idx, int dir, const String &txt, uint32_t col) {
        const FlowRing &R = kFlowRings[idx];
        g.setFont(&fonts::FreeSansBold12pt7b);
        g.setTextDatum(textdatum_t::middle_left);
        g.setTextColor(col, bg);
        const int tw = g.textWidth(txt), aw = dir ? 18 : 0;
        const int y = static_cast<int>(R.cy + kValueDrop + 0.5f);
        int x = static_cast<int>(R.cx + 0.5f) - (tw + aw) / 2;
        if (dir == 1) { g.fillTriangle(x, y - 5, x, y + 5, x + 9, y, col); }
        else if (dir == 2) { g.fillTriangle(x, y - 5, x + 9, y - 5, x + 4, y + 5, col); }
        else if (dir == 3) { g.fillTriangle(x, y + 5, x + 9, y + 5, x + 4, y - 5, col); }
        g.drawString(txt, x + aw, y);
    };
    value(0, s.gridOff ? 0 : 1, s.gridOff ? String("off") : kw(s.gridW), s.gridOff ? rgb(g, kRed) : gridCol);
    value(1, 0, kw(s.loadW), s.gridOff ? homeCol : rgb(g, kInk));
    if (s.battChargeW >= 1) { value(2, 2, kw(s.battChargeW), rgb(g, kBlue)); }
    else if (s.battDischargeW >= 1) { value(2, 3, kw(s.battDischargeW), rgb(g, kAmber)); }
    else { value(2, 0, String("idle"), muted); }

    g.setFont(&fonts::FreeSans12pt7b);
    for (int i = 0; i < 2; ++i)
    {
        I.at(i == 0 ? "Grid" : "Home", static_cast<int>(kFlowRings[i].cx + 0.5f),
             static_cast<int>(kFlowRings[i].cy + kFlowRings[i].r + 18), textdatum_t::middle_center, 90, muted);
    }

    // The dashboard hangs its discharge line below the box; here the box already fills the screen,
    // so the line takes the "Battery" label's slot - it names the battery itself. Both are drawn in
    // the same band at full width, so whichever appears wipes out the other.
    const int bottomY = 306;
    if (s.gridOff && s.leftS)
    {
        I.at(String("Battery discharge time ") + dur(s.leftS), I.W / 2, bottomY,
             textdatum_t::middle_center, I.W - 2 * I.padX, rgb(g, kAmber));
    }
    else
    {
        I.at("Battery", I.W / 2, bottomY, textdatum_t::middle_center, I.W - 2 * I.padX, muted);
    }
}

void DisplayService::drawHistory(const Snap &s)
{
    Impl &I = *_impl;
    lgfx::LGFXBase &g = I.target();
    const int x0 = I.padX + 26, x1 = I.W - I.padX, y0 = I.hdrH + 18, y1 = I.H - 46;
    const int h = y1 - y0, w = x1 - x0;
    g.setFont(&fonts::FreeSans9pt7b);
    for (int p = 0; p <= 100; p += 25)
    {
        const int y = y1 - h * p / 100;
        g.drawFastHLine(x0, y, w, rgb(g, kTrack));
        I.at(String(p), x0 - 6, y, textdatum_t::middle_right, 24, rgb(g, kMuted));
    }
    if (s.slots <= 0)
    {
        I.at("no history yet", I.W / 2, (y0 + y1) / 2, textdatum_t::middle_center, I.W / 2, rgb(g, kMuted));
        return;
    }
    const int bw = w / s.slots;
    for (int i = 0; i < s.slots; ++i)
    {
        const int x = x0 + i * bw;
        const float loadW = s.load[i] * 25.0f;
        const int bh = static_cast<int>(h * fminf(1.0f, loadW / (s.ratingW > 0 ? s.ratingW : 6000.0f)));
        const uint32_t col = s.off[i] == 0 ? kGreen : (s.off[i] >= 15 ? kRed : kAmber);
        if (bh > 0) { g.fillRect(x, y1 - bh, bw > 1 ? bw - 1 : 1, bh, rgb(g, col)); }
    }
    for (int i = 1; i < s.slots; ++i)
    {
        const int xa = x0 + (i - 1) * bw + bw / 2, xb = x0 + i * bw + bw / 2;
        const int ya = y1 - h * s.batt[i - 1] / 100, yb = y1 - h * s.batt[i] / 100;
        g.drawLine(xa, ya, xb, yb, rgb(g, kBlue));
        g.drawLine(xa, ya - 1, xb, yb - 1, rgb(g, kBlue));
    }
    I.at("battery %", x0, I.H - 42, textdatum_t::top_left, 120, rgb(g, kBlue));
    I.at("load, bars by grid state", x1, I.H - 42, textdatum_t::top_right, 260, rgb(g, kMuted));
}

void DisplayService::drawAlerts(const Snap &s)
{
    Impl &I = *_impl;
    lgfx::LGFXBase &g = I.target();
    const int top = I.hdrH + 12, rowH = 34;
    if (s.alerts <= 0)
    {
        g.setFont(&fonts::FreeSans9pt7b);
        I.at("no alerts", I.W / 2, I.H / 2, textdatum_t::middle_center, I.W / 2, rgb(g, kMuted));
        return;
    }
    for (int i = 0; i < s.alerts && i < 7; ++i)
    {
        const int y = top + i * rowH;
        String text;
        uint32_t col = kBlue;
        switch (s.aType[i])
        {
        case 'g': text = "Grid off"; break;
        case 'G': text = "Grid on"; break;
        case 'o': text = "Inverter offline"; col = kRed; break;
        case 'r': text = "Unexpected restart"; col = kRed; break;
        case 'i': text = "Inverter settings saved"; break;
        case 'I': text = "Inverter change refused"; col = kAmber; break;
        case 'b': text = String("Battery below ") + s.aValue[i] + " %"; col = kAmber; break;
        default: text = String("Power ") + String(s.aValue[i] / 10.0f, 1) + " kW"; col = kAmber; break;
        }
        g.fillCircle(I.padX + 5, y + 9, 4, rgb(g, col));
        g.setFont(&fonts::FreeSansBold12pt7b);
        I.at(text, I.padX + 18, y, textdatum_t::top_left, I.W - I.padX - 120, rgb(g, col));
        g.setFont(&fonts::FreeSans9pt7b);
        const uint32_t m = s.aAgeMin[i];
        I.at(m < 60 ? String(m) + "m ago" : (m < 1440 ? String(m / 60) + "h ago" : String(m / 1440) + "d ago"),
             I.W - I.padX, y + 2, textdatum_t::top_right, 100, rgb(g, kMuted));
    }
}

void DisplayService::renderDashboard(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress)
{
    Impl &I = *_impl;
    lgfx::LGFXBase &g = I.target();
    (void)ipAddress;
    Snap s;
#if DISPLAY_DEMO
    fillDemo(s);                                  // history and alert arrays, which have no live source yet
    if (inverterConnected) { fillLive(s, true); } // real (or simulated) values win for everything else
#else
    fillLive(s, inverterConnected);
#endif
    static const char *const kTitles[kPages] = {"Status", "Power flow", "Last 24 h", "Alerts"};

    g.setTextSize(1);
    I.bg = (_page == TFT_DASH_FLOW_PAGE) ? rgb(g, kPanel) : TFT_BLACK; // flow page matches the dashboard
    if (!_drawnOnce || _forceRedraw || _impl->lastRenderedPage != _page)
    {
        g.fillScreen(I.bg);
        _impl->lastRenderedPage = _page;
    }
#if TFT_BOARD_CYD35
    setBoardLed(stateColor(s, nullptr), _ledOverride, _yellowGreen);
#endif
    drawHeader(kTitles[_page], wifiConnected, apMode, inverterConnected);
    latchTouch(millis());
    switch (_page)
    {
    case 1: drawFlow(s); break;
    case 2: drawHistory(s); break;
    case 3: drawAlerts(s); break;
    default: drawStatus(s); break; // page 0: the glance screen draws its own header band
    }
    latchTouch(millis());
}

void DisplayService::render(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress)
{
    renderDashboard(wifiConnected, apMode, inverterConnected, ipAddress);
}

#else // paged layout, used by the small T-Display

void DisplayService::render(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress)
{
    lgfx::LGFXBase &g = _impl->target();
    Impl &I = *_impl;
    g.setTextSize(I.fs);
    if (_impl->lastRenderedPage != _page)
    {
        g.fillScreen(TFT_BLACK);
        _impl->lastRenderedPage = _page;
        _impl->lastTallFont = true;
        _impl->lastBigTextSize = 0.0f;
    }

    static const char *const kTitles[PageCount] = {"BATTERY", "LOAD", "SOLAR", "STATUS"};

    // Header: page title, page dots, link status letters (W = WiFi, I = inverter).
    g.setFont(&fonts::Font2);
    _impl->text(kTitles[_page], I.X(4), I.headerY, textdatum_t::top_left, I.X(90), TFT_LIGHTGREY);

    {
        const bool solarConnected = _settings.get.solarConnected();
        const int dotSpacing = I.X(12);
        const int dotR = static_cast<int>(3 * I.fs);
        const int visible = solarConnected ? PageCount : PageCount - 1;
        const int x0 = I.W / 2 - ((visible - 1) * dotSpacing) / 2;
        g.fillRect(I.W / 2 - I.X(30), I.headerY, I.X(60), static_cast<int>(16 * I.fs), TFT_BLACK);
        int slot = 0;
        for (uint8_t i = 0; i < PageCount; ++i)
        {
            if (i == PageSolar && !solarConnected)
            {
                continue;
            }
            if (i == _page)
            {
                g.fillCircle(x0 + slot * dotSpacing, I.headerY + static_cast<int>(7 * I.fs), dotR, TFT_WHITE);
            }
            else
            {
                g.drawCircle(x0 + slot * dotSpacing, I.headerY + static_cast<int>(7 * I.fs), dotR, TFT_DARKGREY);
            }
            ++slot;
        }
    }

    if (apMode)
    {
        _impl->text("AP", I.W - I.X(4), I.headerY, textdatum_t::top_right, I.X(20), TFT_YELLOW);
    }
    else
    {
        _impl->text("W", I.W - I.X(4), I.headerY, textdatum_t::top_right, I.X(20), wifiConnected ? TFT_GREEN : TFT_RED);
    }
    _impl->text("I", I.W - I.X(26), I.headerY, textdatum_t::top_right, I.X(12), inverterConnected ? TFT_GREEN : TFT_RED);

    const String mode = inverterConnected ? readText(DESCR_Inverter_Operation_Mode) : String();
    String rowA;
    String rowB;

    switch (_page)
    {
    case PageBattery:
    {
        float percentValue = 0;
        int percent = -1;
        if (inverterConnected && readNumber(DESCR_Battery_Percent, percentValue))
        {
            percent = constrain(static_cast<int>(percentValue + 0.5f), 0, 100);
        }
        const uint32_t color = levelColor(percent);
        _impl->drawBigValue(percent < 0 ? String("--") : String(percent), "%", color);

        const int barX = I.X(20);
        const int barY = I.rowAY;
        const int barW = I.X(200);
        const int barH = static_cast<int>(14 * I.fs);
        const int inset = static_cast<int>(2 * I.fs);
        g.drawRoundRect(barX, barY, barW, barH, static_cast<int>(3 * I.fs), TFT_WHITE);
        const int inner = barW - 2 * inset;
        const int fullPct = _settings.get.batteryFullPct() > 0 ? _settings.get.batteryFullPct() : 100; // bar full at this level
        const int fill = percent >= 0 ? (inner * constrain(percent, 0, fullPct)) / fullPct : 0;
        if (fill > 0)
        {
            g.fillRect(barX + inset, barY + inset, fill, barH - 2 * inset, color);
        }
        if (fill < inner)
        {
            g.fillRect(barX + inset + fill, barY + inset, inner - fill, barH - 2 * inset, TFT_BLACK);
        }
        if (inverterConnected)
        {
            rowB = fmtOrDash(DESCR_Battery_Voltage, 1, " V");
            float current = 0;
            if (readBatteryCurrent(current))
            {
                rowB += String("  ") + (current >= 0 ? "+" : "") + fmt(current, 0) + " A";
            }
        }
        break;
    }
    case PageLoad:
    {
        float watts = 0;
        const bool have = inverterConnected && readNumber(DESCR_AC_Out_Watt, watts);
        _impl->drawBigValue(have ? String(static_cast<long>(watts + 0.5f)) : String("--"), "W", have ? TFT_ORANGE : TFT_DARKGREY);
        if (inverterConnected)
        {
            rowA = String("Load ") + fmtOrDash(DESCR_AC_Out_Percent, 0, "%") + "   " + fmtOrDash(DESCR_AC_Out_Voltage, 1, " V") +
                   "  " + fmtOrDash(DESCR_AC_Out_Frequency, 1, " Hz");
            rowB = mode;
        }
        break;
    }
    case PageSolar:
    {
        float watts = 0;
        const bool have = inverterConnected && readNumber(DESCR_PV_Charging_Power, watts);
        _impl->drawBigValue(have ? String(static_cast<long>(watts + 0.5f)) : String("--"), "W", have ? TFT_YELLOW : TFT_DARKGREY);
        if (inverterConnected)
        {
            rowA = String("PV ") + fmtOrDash(DESCR_PV_Input_Voltage, 1, " V") + "   " + fmtOrDash(DESCR_PV_Input_Power, 0, " W in");
            rowB = String("Battery ") + fmtOrDash(DESCR_Battery_Percent, 0, "%");
        }
        break;
    }
    default:
    {
        String text = mode.length() ? mode : String(inverterConnected ? "?" : "offline");
        uint32_t color = TFT_CYAN;
        if (mode.equalsIgnoreCase("Line"))
        {
            color = TFT_GREEN;
        }
        else if (mode.equalsIgnoreCase("Battery"))
        {
            color = TFT_ORANGE;
        }
        else if (mode.equalsIgnoreCase("Fault"))
        {
            color = TFT_RED;
        }
        _impl->drawBigText(text, inverterConnected ? color : TFT_DARKGREY);
        if (inverterConnected)
        {
            rowA = String("Grid ") + fmtOrDash(DESCR_AC_In_Voltage, 1, " V") + "  " + fmtOrDash(DESCR_AC_In_Frequency, 1, " Hz") +
                   "  " + fmtOrDash(DESCR_Inverter_Bus_Temperature, 0, " C");
            rowB = String("Bat ") + fmtOrDash(DESCR_Battery_Voltage, 1, " V") + "  PV " + fmtOrDash(DESCR_PV_Charging_Power, 0, " W");
        }
        break;
    }
    }

    g.setFont(&fonts::Font2);
    g.setTextSize(I.fs);
    if (_page != PageBattery)
    {
        _impl->text(rowA, I.X(4), I.rowAY, textdatum_t::top_left, I.W - I.X(8), TFT_LIGHTGREY);
    }
    const String leftB = !inverterConnected ? String("no inverter data") : rowB;
    _impl->text(leftB, I.X(4), I.rowBY, textdatum_t::top_left, I.X(116), TFT_LIGHTGREY);
    if (apMode)
    {
        _impl->text("AP: Solar2MQTT", I.W - I.X(4), I.rowBY, textdatum_t::top_right, I.X(112), TFT_YELLOW);
    }
    else
    {
        _impl->text(ipAddress, I.W - I.X(4), I.rowBY, textdatum_t::top_right, I.X(112), TFT_LIGHTGREY);
    }
}

#endif // TFT_DASH_PAGES

#else // !HAS_TFT

void DisplayService::begin() {}
void DisplayService::loop(bool, bool, bool, bool, const String &) {}
int DisplayService::panelWidth() const { return 0; }
int DisplayService::panelHeight() const { return 0; }
bool DisplayService::readRow(int, uint8_t *, int) { return false; }
void DisplayService::setPage(uint8_t) {}

#endif
