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
    String touchDebug() const; // tap and repaint counters, for chasing missed taps
    // Blocks until the display loop has started a fresh pass (or the timeout). After setRedrawHold(true) that pass has seen
    // the hold, and the pass before it - which may have been drawing - is over, so the panel can be read safely.
    bool waitForLoopPass(uint32_t timeoutMs);
    // Sample the touch controller outside DisplayService::loop's own once-per-iteration poll.
    // The XPT2046 only registers about 44% of polls during a press, so the cure for a missed
    // tap is more samples spread across the loop, not a longer press.
    void pumpTouch();
    void setPage(uint8_t page);
    void setRedrawHold(bool on); // freeze the panel while it is being read back
    void setLedOverride(int mode, int yellowGreen); // 0 auto, 1 green, 2 yellow, 3 red, 4 off
    void requestTouchCalibration();
    bool calibrationValues(uint16_t *out) const;

    struct Snap; // the values the screens draw; public so the state helper can read them

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
    static constexpr uint32_t kReturnToFlowMs = 60000; // any other page falls back to the power flow after a minute
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
    std::atomic<bool> _holdRedraw {false};
    volatile int _ledOverride = 0;
    volatile int _yellowGreen = 45; // green is far brighter than red, so yellow needs it turned well down
    std::atomic<uint32_t> _holdSinceMs {0}; // written before _holdRedraw, so a new hold is never paired with an old time
    std::atomic<uint32_t> _loopSeq {0};     // bumped at the start of every loop() pass: lets a reader wait for the loop
    bool _calibDone = false;
    uint16_t _calib[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // sample the touch and remember the step, including mid-redraw

    bool pollButton(Button &button, uint32_t now);
    void render(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress);
    void renderDashboard(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress);
    void fillLive(Snap &s, bool inverterConnected);
    void drawHeader(const char *title, bool wifiConnected, bool apMode, bool inverterConnected);
    void drawSummary(const Snap &s);
    void drawFlow(const Snap &s);
    // With wn > 0 only the segments under those wipe discs are repainted: redrawing all three
    // paths costs 136 anti-aliased lines, far too much for a 70 ms animation tick.
    void drawFlowPaths(const int *wx = nullptr, const int *wy = nullptr, int wn = 0, float rad = 0.0f);
    // Repaint one ring's stroke band, so a dot that ran under it comes out behind it again.
    void strokeRing(int idx);

    void animateFlow(uint32_t now);

    // Power-flow link animation: which links carry power, their colour, how long a dot takes to
    // cross, and where each dot is now so the next frame can rub it out.
    bool _flowOn[3] = {false, false, false};
    uint32_t _flowCol[3] = {0, 0, 0};
    float _flowDur[3] = {2.0f, 2.0f, 2.0f};
    float _flowPhase[3] = {0.0f, 0.33f, 0.66f};
    int _flowDotX[3] = {-1, -1, -1};
    int _flowDotY[3] = {-1, -1, -1};
    uint32_t _flowLastMs = 0;
    float _ringFrac[3] = {0.0f, 0.0f, 0.0f};
    uint32_t _ringCol[3] = {0, 0, 0};
    bool _ringShow[3] = {false, false, false};
    uint32_t _ringTrackCol = 0;
    uint32_t _lastPumpMs = 0;
    uint32_t _pageSetMs = 0; // when the page was last chosen, by a tap or remotely
    uint32_t _dgPollLastMs = 0, _dgMaxPollGap = 0, _dgMaxRenderMs = 0, _dgLastRenderMs = 0;
    uint32_t _dgContacts = 0, _dgRising = 0, _dgAccepted = 0, _dgLockedOut = 0, _dgPolls = 0;
    uint32_t _dgRenders = 0, _dgRenderMsTotal = 0;
    // A single max hides a stall that repeats. Keep the last long gaps with their timestamps.
    static constexpr int kGapLog = 12;
    uint32_t _dgGapMs[kGapLog] = {0};
    uint32_t _dgGapAt[kGapLog] = {0};
    uint8_t _dgGapHead = 0;
    uint32_t _dgLongGaps = 0;
    void drawHistory(const Snap &s);
    void drawAlerts(const Snap &s);
    String buildSignature(bool wifiConnected, bool apMode, bool inverterConnected, const String &ipAddress) const;
#endif
};
