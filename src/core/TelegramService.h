#pragma once

#include <Arduino.h>
#include <atomic>
#include <functional>

// Telegram bot front end: answers /summary (or the "Summary" button) with an inverter summary
// and removes the previous summary message so the chat only keeps the latest one.
// Runs its own FreeRTOS task with one persistent TLS connection to api.telegram.org.
// Compiled to an empty stub unless HAS_TELEGRAM=1.
class TelegramService
{
public:
    void begin(std::function<bool()> networkConnected);
    void loop(bool inverterConnected, int wifiRssi);
    void reconfigure();
    bool requestSummary();
    bool isReady() const;
    String statusJson() const;

private:
#if HAS_TELEGRAM
    struct Impl;
    Impl *_impl = nullptr;
#endif
};
