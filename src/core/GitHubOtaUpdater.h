#pragma once

#include <Arduino.h>
#include <functional>

class GitHubOtaUpdater
{
public:
    enum class State : uint8_t
    {
        Idle,
        Checking,
        UpToDate,
        UpdateAvailable,
        Downloading,
        Success,
        Error
    };

    GitHubOtaUpdater(const char *owner,
                     const char *repo,
                     const char *currentVersion,
                     const char *buildVariant);

    void begin(std::function<bool()> networkConnected);
    bool requestCheck();
    bool startUpdate();
    String statusJson() const;
    bool isBusy() const;
    // Called with true before the updater opens its TLS connection and with false afterwards, so other TLS
    // users (the Telegram bot) can release their memory first; two sessions do not fit on a classic ESP32.
    void setNetworkPauseHook(std::function<void(bool)> hook) { _pauseHook = std::move(hook); }
    State state() const;
    String latestVersion() const;
    String lastError() const;
    const String &currentVersion() const { return _currentVersion; }

private:
    static void checkTask(void *param);
    static void downloadTask(void *param);

    void doCheck();
    void doDownload();
    void scheduleRestart(uint32_t delayMs);
    void setState(State state, const String &error = "");
    void clearTask();

    static int compareVersions(const String &left, const String &right);
    static String normalizeVersion(const String &value);

    void lock() const;
    void unlock() const;

    String _owner;
    String _repo;
    String _currentVersion;
    String _buildVariant;

    String _latestVersion;
    String _assetUrl;
    String _assetName;
    String _lastError;

    uint32_t _bytesTotal = 0;
    uint32_t _bytesDone = 0;
    uint32_t _stateTs = 0;
    State _state = State::Idle;

    void *_lockHandle = nullptr;
    void *_task = nullptr;
    std::function<bool()> _networkConnected;
    std::function<void(bool)> _pauseHook;
};
