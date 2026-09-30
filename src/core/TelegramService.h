#pragma once

#include <Arduino.h>
#include <atomic>
#include <functional>

// Telegram bot front end: answers /summary (or the "Summary" button) with an inverter summary
// and removes the previous summary message so the chat only keeps the latest one.
// Runs its own FreeRTOS task with one persistent TLS connection to api.telegram.org.
// Compiled to an empty stub unless HAS_TELEGRAM=1.
class GitHubOtaUpdater;

class TelegramService
{
public:
    void begin(std::function<bool()> networkConnected);
    void loop(bool inverterConnected, int wifiRssi);
    void reconfigure();
    bool requestSummary();
    bool isReady() const;
    // Close the bot's TLS connection and keep it closed, so another TLS client (the firmware updater) has
    // enough memory. Blocks until the connection is released or timeoutMs passes; resume() reopens it.
    bool pause(uint32_t timeoutMs = 30000);
    void resume();
    // Firmware updater used by /upgrade, the periodic update check and the summary's "new version" line.
    void setUpdater(GitHubOtaUpdater *updater);
    // Sends a raw command to the inverter (the main loop's SolarInverterService); used for the Dashboard's inverter settings.
    void setInverterCommandHook(std::function<void(const String &)> hook);
    String statusJson() const;

    // The board's own screens read the same two records the Mini App does: the 24 h history kept in
    // RTC and the alert log kept in flash. Both return how many entries were written.
    int historySnapshot(uint8_t *batt, uint8_t *load, uint8_t *off, int maxSlots) const;
    int alertSnapshot(char *type, uint16_t *value, uint32_t *ageMin, int maxAlerts) const;

private:
#if HAS_TELEGRAM
    struct Impl;
    Impl *_impl = nullptr;
#endif
};
