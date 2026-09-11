#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_ota_ops.h>

#include "core/DiagLog.h"
#include "core/Ds18b20Service.h"
#include "core/FactoryResetManager.h"
#include "core/GitHubOtaUpdater.h"
#include "core/InternalTemperatureService.h"
#include "core/LogSerial.h"
#include "core/MqttHandler.h"
#include "core/SettingsPrefs.h"
#include "core/SolarState.h"
#include "core/StatusLedService.h"
#include "core/WebServerHandler.h"
#include "core/WiFiManager.h"
#include "main.h"
#include "pins.h"
#if HAS_TFT
#include "core/DisplayService.h"
#endif
#if HAS_TELEGRAM
#include "core/TelegramService.h"
#endif
#include "solar/SolarInverterService.h"

// A newly installed firmware stays on probation: if it restarts before it has proven itself (Telegram connected,
// or three minutes of uptime), the bootloader rolls back to the previous image. Overrides the core's weak hook.
extern "C" bool verifyRollbackLater()
{
    return true;
}

Settings _settings;

#ifndef OTA_GITHUB_OWNER
#error "OTA_GITHUB_OWNER must be defined via build flags"
#endif

#ifndef OTA_GITHUB_REPO
#error "OTA_GITHUB_REPO must be defined via build flags"
#endif

#ifndef BUILD_VARIANT
#error "BUILD_VARIANT must be defined via build flags"
#endif

bool g_pendingRestart = false;
uint32_t g_restartAt = 0;
bool g_pendingNetworkReconfigure = false;
uint32_t g_networkReconfigureAt = 0;
bool g_pendingInverterReconfigure = false;
uint32_t g_inverterReconfigureAt = 0;
uint8_t g_bootBannerRepeats = 0;
uint32_t g_bootBannerNextAt = 0;
#ifdef DIAG_CRASH_TEST
volatile bool g_crashTestRequested = false;
#endif

SolarState solarState;
AsyncWebServer server(80);
WiFiManager wifiManager(server);
SolarInverterService inverterService(solarState);
Ds18b20Service ds18b20Service;
InternalTemperatureService internalTemperatureService;
MqttHandler mqttHandler(solarState, wifiManager, inverterService);
StatusLedService statusLedService;
#if HAS_TFT
DisplayService displayService;
#endif
#if HAS_TELEGRAM
TelegramService telegramService;
#endif
GitHubOtaUpdater otaUpdater(OTA_GITHUB_OWNER, OTA_GITHUB_REPO, STRVERSION, BUILD_VARIANT);
WebServerHandler webServerHandler(server, wifiManager, solarState, inverterService, mqttHandler, otaUpdater);

namespace
{
void printBootBanner()
{
    LogSerial.printf("[Boot] %s %s (%s) t=%lu ms\n", SOURCE_NAME, STRVERSION, BUILD_VARIANT, static_cast<unsigned long>(millis()));
    LogSerial.printf("[Boot] Heap free: %u bytes\n", static_cast<unsigned>(ESP.getFreeHeap()));
}

void configureDs18b20()
{
    ds18b20Service.begin(static_cast<uint8_t>(_settings.get.ds18b20Pin()), liveData);
    solarState.refreshBindings();
}

void updateRuntimeState()
{
    solarState.updateRuntime(_settings.get.deviceName(),
                             inverterService.protocol(),
                             inverterService.isConnected(),
                             wifiManager.getConnectionState(),
                             mqttHandler.isConnected(),
                             wifiManager.isEthActive(),
                             wifiManager.isInApMode(),
                             wifiManager.rssi(),
                             wifiManager.ipAddress());
    solarState.refreshBindings();

    JsonObject statusObject = solarState.status();
    statusObject["loopbackRunning"] = inverterService.loopbackRunning();
    statusObject["loopbackDone"] = inverterService.loopbackDone();
    statusObject["loopbackOk"] = inverterService.loopbackOk();
    statusObject["loopbackMessage"] = inverterService.loopbackMessage();
    statusObject["simulationEnabled"] = inverterService.simulationEnabled();
    statusObject["simulationProtocol"] = inverterService.simulationEnabled() ? "PI30" : "";
    solarState.doc()["EspData"]["Simulation_Active"] = inverterService.simulationEnabled();
    solarState.doc()["EspData"]["Simulation_Mode"] = inverterService.simulationEnabled() ? "PI30" : "";
    solarState.refreshBindings();
}
} // namespace

// Mark a new OTA image as valid once it has proven itself, cancelling the pending rollback.
static void confirmFirmwareOnce()
{
    static bool done = false;
    if (done)
    {
        return;
    }
#if HAS_TELEGRAM
    const bool proven = telegramService.isReady() || millis() > 180000UL;
#else
    const bool proven = millis() > 180000UL;
#endif
    if (!proven)
    {
        return;
    }
    done = true;
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        esp_ota_mark_app_valid_cancel_rollback();
        LogSerial.println("[OTA] New firmware confirmed, rollback cancelled");
    }
}

void setup()
{
    DiagLog::begin(); // first: why the board restarted, and the log that survived the restart
    LogSerial.begin(MONITOR_SPEED);
    printBootBanner();
    LogSerial.printf("[Diag] Last restart: %s\n", DiagLog::resetReasonText());
    if (DiagLog::crashReportPending())
    {
        LogSerial.printf("[Diag] %s\n", DiagLog::crashReportText().c_str());
    }
    g_bootBannerRepeats = 1;
    g_bootBannerNextAt = millis() + 1500;
    delay(150);

    _settings.begin();
    solarState.begin();
#if HAS_TFT
    displayService.begin();
#endif
    statusLedService.begin(_settings.get.statusLedPin(),
                           static_cast<uint8_t>(_settings.get.statusLedBrightness()));

    FactoryResetManager::begin(10000, 6);
    wifiManager.begin();
    DiagLog::beginNetwork();
    otaUpdater.begin([]() { return wifiManager.getConnectionState(); });
#if HAS_TELEGRAM
    telegramService.begin([]() { return wifiManager.getConnectionState() && !wifiManager.isInApMode(); });
    webServerHandler.setTelegramService(&telegramService);
    telegramService.setUpdater(&otaUpdater);
    otaUpdater.setNetworkPauseHook([](bool pause) {
        if (pause)
        {
            telegramService.pause();
        }
        else
        {
            telegramService.resume();
        }
    });
#endif

    inverterService.setCallback([]()
                                {
        updateRuntimeState();
        webServerHandler.setMqttConnected(mqttHandler.isConnected());
        webServerHandler.setInverterConnected(inverterService.isConnected());
        mqttHandler.triggerFullStatePublish();
        webServerHandler.notifyStatusBar(); });
    inverterService.setTransportPaused(wifiManager.isInApMode());
    inverterService.begin();

    ds18b20Service.setCallback([](uint8_t index, float temperature)
                               {
        mqttHandler.publishSensorImmediate(index, temperature);
        mqttHandler.triggerFullStatePublish();
        webServerHandler.notifyStatusBar(); });
    configureDs18b20();

    internalTemperatureService.setCallback([]()
                                           {
        mqttHandler.triggerFullStatePublish();
        webServerHandler.notifyStatusBar(); });
    internalTemperatureService.begin(solarState.doc()["EspData"].as<JsonObject>());

    mqttHandler.begin();
    webServerHandler.begin();
#ifdef DIAG_CRASH_TEST
    // Test builds only: "crashtest" typed in the web serial console crashes the board from the main loop, to
    // exercise crash reports.
    LogSerial.onMessage([](const std::string &msg) {
        if (msg.find("crashtest") != std::string::npos)
        {
            g_crashTestRequested = true;
        }
    });
#endif

    updateRuntimeState();
    webServerHandler.setMqttConnected(mqttHandler.isConnected());
    webServerHandler.setInverterConnected(inverterService.isConnected());
    webServerHandler.notifyStatusBar();
}

void loop()
{
    if (g_bootBannerRepeats < 4 && static_cast<int32_t>(millis() - g_bootBannerNextAt) >= 0)
    {
        ++g_bootBannerRepeats;
        g_bootBannerNextAt = millis() + 1500;
        printBootBanner();
    }

    DiagLog::tick();
#ifdef DIAG_CRASH_TEST
    if (g_crashTestRequested)
    {
        LogSerial.println("[Diag] Crash test: writing to address 0");
        volatile uint32_t *bad = nullptr;
        *bad = 1;
    }
#endif
    FactoryResetManager::loop();
    wifiManager.loop();
    if (g_pendingNetworkReconfigure && static_cast<int32_t>(millis() - g_networkReconfigureAt) >= 0)
    {
        g_pendingNetworkReconfigure = false;
        wifiManager.reconfigure();
        mqttHandler.triggerFullStatePublish();
        if (_settings.get.mqttHAEnabled())
        {
            mqttHandler.triggerHaDiscovery();
        }
        webServerHandler.notifyStatusBar();
    }
    inverterService.setTransportPaused(wifiManager.isInApMode());
    inverterService.loop();
    ds18b20Service.loop();
    internalTemperatureService.loop();
    mqttHandler.loop();

    if (g_pendingInverterReconfigure && static_cast<int32_t>(millis() - g_inverterReconfigureAt) >= 0)
    {
        g_pendingInverterReconfigure = false;
        inverterService.reconfigure();
        configureDs18b20();
        statusLedService.configure(_settings.get.statusLedPin(),
                                   static_cast<uint8_t>(_settings.get.statusLedBrightness()));
        mqttHandler.triggerFullStatePublish();
        webServerHandler.notifyStatusBar();
    }

    if (g_pendingRestart && static_cast<int32_t>(millis() - g_restartAt) >= 0)
    {
        g_pendingRestart = false;
        inverterService.shutdown();
        delay(25);
        ESP.restart();
    }

    updateRuntimeState();
    webServerHandler.setMqttConnected(mqttHandler.isConnected());
    webServerHandler.setInverterConnected(inverterService.isConnected());
    webServerHandler.loop();
#if HAS_TELEGRAM
    telegramService.loop(inverterService.isConnected(), wifiManager.rssi());
#endif
    confirmFirmwareOnce();
#if HAS_TFT
    displayService.loop(wifiManager.getConnectionState(),
                        wifiManager.isInApMode(),
                        mqttHandler.isConnected(),
                        inverterService.isConnected(),
                        wifiManager.ipAddress());
#endif
    statusLedService.loop(wifiManager.getConnectionState(),
                          strlen(_settings.get.mqttHost()) > 0,
                          mqttHandler.isConnected(),
                          inverterService.isConnected());

    delay(1);
}
