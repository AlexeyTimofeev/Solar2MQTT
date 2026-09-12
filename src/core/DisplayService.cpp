#include "DisplayService.h"

#if HAS_TFT

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>

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
        g.setTextColor(color, TFT_BLACK);
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
        g.setTextColor(color, TFT_BLACK);
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
    _impl->tft.setBrightness(200);
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

    _page = PageBattery;
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

void DisplayService::loop(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress)
{
    if (_impl == nullptr)
    {
        return;
    }
    const uint32_t now = millis();

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
        const int fill = percent >= 0 ? (inner * percent) / 100 : 0;
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

#else // !HAS_TFT

void DisplayService::begin() {}
void DisplayService::loop(bool, bool, bool, bool, const String &) {}

#endif
