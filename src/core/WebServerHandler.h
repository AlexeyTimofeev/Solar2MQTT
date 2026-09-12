#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class WiFiManager;
class SolarState;
class SolarInverterService;
class GitHubOtaUpdater;
class TelegramService;

class WebServerHandler
{
public:
    WebServerHandler(AsyncWebServer &server,
                     WiFiManager &wifiManager,
                     SolarState &state,
                     SolarInverterService &inverterService,
                     GitHubOtaUpdater &otaUpdater);

    void begin();
    void loop();
    void setInverterConnected(bool value) { _inverterConnected = value; }
    void notifyStatusBar();
    void setTelegramService(TelegramService *service) { _telegram = service; }

private:
    static WebServerHandler *s_self;

    AsyncWebServer &_server;
    WiFiManager &_wifiManager;
    SolarState &_state;
    SolarInverterService &_inverterService;
    GitHubOtaUpdater &_otaUpdater;
    TelegramService *_telegram = nullptr;
    bool _inverterConnected;
    std::atomic<bool> _statusDirty;
    uint32_t _lastStatusRefreshMs;
    String _lastStatusPayload;
    SemaphoreHandle_t _statusPayloadMutex;

    bool isAuthorized(AsyncWebServerRequest *request);
    void registerRoutes();
    void buildStatusJson(JsonDocument &doc);
    void refreshStatusPayload();
    String lastStatusPayloadCopy();
    void setLastStatusPayload(const String &payload);
    void sendAsset(AsyncWebServerRequest *request, const char *mime, const uint8_t *data, size_t len);
};
