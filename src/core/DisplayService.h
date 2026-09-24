#pragma once

#include <Arduino.h>
#include <atomic>

// TTGO T-Display TFT front end.
// Shows the battery state of charge by default; the two on-board buttons page through
// battery, load, solar and inverter status views. Compiled to an empty stub unless HAS_TFT=1.
class DisplayService
{
public:
    void begin();
    void loop(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress);
    // Debug builds only: read the panel back for screenshots and switch screens over HTTP.
    int panelWidth() const;
    int panelHeight() const;
    bool readRow(int y, uint8_t *bgr, int width);
    void setPage(uint8_t page);
    void setRedrawHold(bool on); // freeze the panel while it is being read back
    void requestTouchCalibration();
    bool calibrationValues(uint16_t *out) const;

private:
#if HAS_TFT
    struct Impl;
    Impl *_impl = nullptr;

    enum Page : uint8_t
    {
        PageBattery = 0,
        PageLoad,
        PageSolar,
        PageStatus,
        PageCount
    };

    struct Button
    {
        int pin = -1;
        bool stableLevel = true;
        bool lastRead = true;
        uint32_t lastChangeMs = 0;
    };

    static constexpr uint32_t kPollIntervalMs = 1000;
    static constexpr uint32_t kForceRedrawMs = 10000;
    static constexpr uint32_t kDebounceMs = 40;
    static constexpr uint32_t kTouchLockoutMs = 120; // one tap per 120 ms: fast tapping must not drop pages
    static constexpr uint32_t kTouchReleaseMs = 60;

    Button _btnPrev;
    Button _btnNext;
    uint8_t _page = PageBattery;

    uint32_t _lastPollMs = 0;
    uint32_t _lastDrawMs = 0;
    bool _drawnOnce = false;
    bool _forceRedraw = false;

    String _lastSignature;
    bool _touchWasDown = false;
    uint32_t _touchDownMs = 0;
    uint32_t _touchLastSeenMs = 0;
    bool pollTouch(uint32_t now, bool &next);
    // Touch runs in its own task: the main loop is paced by the inverter poll at ~10 Hz, which is too
    // coarse to catch a quick tap. The mutex keeps touch reads off the SPI bus while the screen draws.
    std::atomic<int> _tapSteps {0};
    void latchTouch(uint32_t now);
    volatile bool _calibRequested = false;
    volatile bool _holdRedraw = false;
    uint32_t _holdSinceMs = 0;
    bool _calibDone = false;
    uint16_t _calib[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // sample the touch and remember the step, including mid-redraw

    bool pollButton(Button &button, uint32_t now);
    void render(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress);
    void renderDashboard(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress);
    struct Snap;
    void fillDemo(Snap &s);
    void fillLive(Snap &s, bool inverterConnected);
    void drawHeader(const char *title, bool wifiConnected, bool apMode, bool inverterConnected);
    void drawStatus(const Snap &s);
    void drawSummary(const Snap &s);
    void drawFlow(const Snap &s);
    void drawHistory(const Snap &s);
    void drawAlerts(const Snap &s);
    String buildSignature(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress) const;
#endif
};
