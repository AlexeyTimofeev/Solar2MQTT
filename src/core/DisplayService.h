#pragma once

#include <Arduino.h>

// TTGO T-Display TFT front end.
// Shows the battery state of charge by default; the two on-board buttons page through
// battery, load, solar and inverter status views. Compiled to an empty stub unless HAS_TFT=1.
class DisplayService
{
public:
    void begin();
    void loop(bool wifiConnected, bool apMode, bool mqttConnected, bool inverterConnected, const String &ipAddress);

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
    static constexpr uint32_t kTouchLockoutMs = 300;
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

    bool pollButton(Button &button, uint32_t now);
    void render(bool wifiConnected, bool apMode, bool mqttConnected, bool inverterConnected, const String &ipAddress);
    String buildSignature(bool wifiConnected, bool apMode, bool mqttConnected, bool inverterConnected, const String &ipAddress) const;
#endif
};
