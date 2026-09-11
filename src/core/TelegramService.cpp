#include "core/TelegramService.h"

#if HAS_TELEGRAM

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <vector>

#include "core/DiagLog.h"
#include "core/GitHubOtaUpdater.h"
#include "core/LogSerial.h"
#include "core/SettingsPrefs.h"
#include "core/SolarState.h"
#include "core/TelegramRootCa.h"
#include "main.h"
#include "descriptors.h"

extern Settings _settings;

namespace
{
constexpr uint32_t kTaskStack = 16384;
constexpr uint32_t kLongPollSeconds = 20;
constexpr uint32_t kPollHttpTimeoutMs = (kLongPollSeconds + 15) * 1000;
constexpr uint32_t kSendHttpTimeoutMs = 15000;
constexpr uint32_t kSnapshotIntervalMs = 2000;
constexpr uint32_t kMinSummaryGapMs = 3000;
constexpr uint32_t kErrorBackoffMs = 15000;
constexpr uint32_t kIdleDelayMs = 1000;
constexpr const char *kApiHost = "https://api.telegram.org/bot";
constexpr const char *kPrefsNamespace = "tg";
constexpr int kBatteryAlertLevels[] = {30, 25, 20, 15, 10};
constexpr size_t kBatteryAlertCount = sizeof(kBatteryAlertLevels) / sizeof(kBatteryAlertLevels[0]);
constexpr int kBatteryRearmMargin = 3;
constexpr uint32_t kBatteryConfirmMs = 30000; // a level must stay crossed this long before it alerts
constexpr uint32_t kAutoSummaryIntervalMs = 60000;
constexpr uint32_t kBootSummaryDelayMs = 45000; // first summary after a restart, once the inverter values are in
constexpr uint32_t kFirstUpdateCheckMs = 120000;                 // first look for new firmware after boot
constexpr uint32_t kUpdateCheckIntervalMs = 12UL * 3600UL * 1000UL; // then twice a day
constexpr int kLoadAlertOnPercent = 80;
constexpr int kLoadAlertOffPercent = 70;

#ifndef DASHBOARD_URL
#define DASHBOARD_URL ""
#endif
constexpr uint32_t kDashSlotSeconds = 900; // dashboard history: one slot per 15 minutes
constexpr size_t kDashSlots = 96;           // 24 hours
constexpr uint32_t kDashMagic = 0x44534831; // "DSH1"
constexpr uint8_t kDashNone = 255;          // slot without data

// Dashboard history in RTC memory: survives a crash, watchdog or software restart (firmware update), not a power cut.
struct DashHistory
{
    uint32_t magic;
    uint32_t count;           // slots in use
    uint32_t head;            // next slot to write
    int64_t lastSlotEnd;      // unix time the newest slot closed, 0 when the clock was unknown
    int64_t outageStart;      // unix time the grid went off, 0 while the grid is on or the start is unknown
    uint8_t batt[kDashSlots]; // battery % at the end of the slot
    uint8_t load[kDashSlots]; // average load in 25 W steps
    uint8_t off[kDashSlots];  // minutes without grid in the slot
};
RTC_NOINIT_ATTR DashHistory dashHist;

// Unix time from SNTP, or 0 until the clock has been set.
int64_t unixNow()
{
    const time_t now = time(nullptr);
    return now > 1700000000 ? static_cast<int64_t>(now) : 0;
}

String base64Url(const uint8_t *data, size_t len)
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    String out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3)
    {
        const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (i + 1 < len ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
                           (i + 2 < len ? data[i + 2] : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        if (i + 1 < len) out += kAlphabet[(n >> 6) & 63];
        if (i + 2 < len) out += kAlphabet[n & 63];
    }
    return out;
}

String urlEncode(const String &in)
{
    static const char kHex[] = "0123456789ABCDEF";
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i)
    {
        const uint8_t c = static_cast<uint8_t>(in[i]);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += static_cast<char>(c);
        }
        else
        {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

String htmlEscape(const String &in)
{
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i)
    {
        const char c = in[i];
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

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

// Rated values and settings read from the inverter (QPIRI), e.g. the output source priority.
String readStaticText(const char *key)
{
    if (staticData.isNull())
    {
        return String();
    }
    JsonVariant value = staticData[key];
    return value.isNull() ? String() : value.as<String>();
}

String num(const char *key, uint8_t decimals, const char *unit, bool *ok = nullptr)
{
    float value = 0;
    if (!readNumber(key, value))
    {
        if (ok) *ok = false;
        return String("--") + unit;
    }
    if (ok) *ok = true;
    return String(value, static_cast<unsigned int>(decimals)) + unit;
}

// Drops "Ok"/"0" placeholders, every PV related entry when no PV is connected, and "Line fail" while running on
// battery (the mode line already says the grid is out).
String filterAlerts(const String &raw, bool solarConnected, bool onBattery)
{
    String text = raw;
    text.trim();
    if (text.length() == 0 || text == "0" || text == "00" || text.equalsIgnoreCase("Ok"))
    {
        return String();
    }
    if (solarConnected && !onBattery)
    {
        return text;
    }
    String out;
    int start = 0;
    while (start <= static_cast<int>(text.length()))
    {
        int end = text.length();
        for (const char sep : {',', ';', '|'})
        {
            const int idx = text.indexOf(sep, start);
            if (idx >= 0 && idx < end)
            {
                end = idx;
            }
        }
        String item = text.substring(start, end);
        item.trim();
        String upper = item;
        upper.toUpperCase();
        const bool dropPv = !solarConnected && upper.indexOf("PV") >= 0;
        const bool dropLineFail = onBattery && upper.indexOf("LINE FAIL") >= 0;
        if (item.length() && !dropPv && !dropLineFail)
        {
            if (out.length()) out += ", ";
            out += item;
        }
        start = end + 1;
    }
    return out;
}

// Single moon-phase glyph for a 0..100 value: 🌑 🌘 🌗 🌖 🌕, rounded to the nearest quarter.
String bar10(int percent, bool /*highIsBad*/)
{
    static const char *const kPhases[] = {
        "\xF0\x9F\x8C\x91", // 🌑 ~0 %
        "\xF0\x9F\x8C\x98", // 🌘 ~25 %
        "\xF0\x9F\x8C\x97", // 🌗 ~50 %
        "\xF0\x9F\x8C\x96", // 🌖 ~75 %
        "\xF0\x9F\x8C\x95", // 🌕 ~100 %
    };
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return String(kPhases[(percent * 4 + 50) / 100]);
}

String uptimeText()
{
    uint32_t s = millis() / 1000;
    const uint32_t d = s / 86400; s %= 86400;
    const uint32_t h = s / 3600; s %= 3600;
    const uint32_t m = s / 60;
    String out;
    if (d) out += String(d) + "d ";
    if (d || h) out += String(h) + "h ";
    out += String(m) + "m";
    return out;
}
} // namespace

struct TelegramService::Impl
{
    // Configuration copied from settings (owned by the main thread, read by the task under lock).
    bool enabled = false;
    bool autoSummary = false;
    String token;
    String chatIdsRaw;
    std::vector<String> chatIds; // parsed from chatIdsRaw, comma separated

    // Per-chat runtime state (task-owned): last summary message and send time.
    struct ChatState
    {
        String id;
        int64_t lastMsgId = 0;
        uint32_t lastSummaryMs = 0;
    };
    std::vector<ChatState> chats;
    uint32_t lastAutoSummaryMs = 0;

    // Runtime state.
    std::function<bool()> networkConnected;
    SemaphoreHandle_t lock = nullptr;
    TaskHandle_t task = nullptr;
    std::atomic<bool> configChanged {false};
    std::atomic<bool> summaryRequested {false};
    std::atomic<bool> ready {false};
    std::atomic<bool> pauseRequested {false};
    std::atomic<bool> pausedAck {false};
    GitHubOtaUpdater *updater = nullptr;
    std::atomic<uint8_t> upgradeStage {0}; // Upgrade button or /upgrade: 0 idle, 1 checking, 2 installing (bot task writes)
    String upgradeChat;
    String offeredVersion;                 // newest release the updater has seen; survives a failed download (bot task)
    bool firstReadyHandled = false;
    bool bootSummaryPending = false;       // first summary after a restart, posted kBootSummaryDelayMs after boot (bot task)
    uint32_t lastUpdateCheckMs = 0;        // main thread: periodic firmware check
    std::atomic<bool> diagRequested {false}; // /diag: the bot task asks, the main thread builds diagText
    std::atomic<bool> diagReady {false};
    String diagText;                         // under lock
    uint8_t crashReportTries = 0;            // bot task
    uint32_t lastCrashTryMs = 0;

    String summarySnapshot;
    uint32_t snapshotMs = 0;
    uint32_t lastSnapshotBuildMs = 0;
    bool lastInverterConnected = false;
    int lastRssi = 0;

    String botUsername;
    String lastError;
    uint32_t lastSummaryMs = 0;
    uint32_t summariesSent = 0;
    int64_t updateOffset = 0;

    WiFiClientSecure client;
    HTTPClient http;
    Preferences prefs;
    std::vector<String> pendingLogs; // written by the task, flushed by loop() on the main thread
    std::vector<String> pendingAlerts; // written by loop() on the main thread, sent by the task
    int lastBatteryPercent = -1;
    bool loadAlertArmed = true;
    std::atomic<bool> loudSummaryRequested {false};
    String loudHeadline;              // set together with loudSummaryRequested (under lock)
    bool inverterWasUp = false;       // inverter link state tracking (main thread)
    uint32_t inverterDownSinceMs = 0;
    bool inverterOfflineNotified = false;
    uint32_t lastOfflineNoticeMs = 0;

    // Dashboard button: the main thread records the history and builds the link, the bot task reads it under lock.
    String dashboardUrl;
    std::atomic<uint8_t> dashSlotsAllowed {static_cast<uint8_t>(kDashSlots)}; // halves when Telegram rejects the link
    std::atomic<bool> dashDisabled {false};
    uint32_t slotStartMs = 0;
    uint32_t slotLoadSum = 0;
    uint16_t slotSamples = 0;
    uint16_t slotOffSeconds = 0;
    int slotBatt = -1;
    bool gridWasOff = false;
    uint32_t outageStartMs = 0;

    void requestLoud(const String &headline)
    {
        lockTake();
        loudHeadline = headline;
        lockGive();
        loudSummaryRequested = true;
    }

    // Main thread: one loud summary when the inverter link stays down for 15 s (max one per 5 min).
    void checkInverterLink(bool inverterConnected)
    {
        const uint32_t now = millis();
        if (inverterConnected)
        {
            inverterWasUp = true;
            inverterDownSinceMs = 0;
            inverterOfflineNotified = false;
            return;
        }
        if (!inverterWasUp)
        {
            return; // never seen the inverter yet (boot), nothing to report
        }
        if (inverterDownSinceMs == 0)
        {
            inverterDownSinceMs = now;
            return;
        }
        if (!inverterOfflineNotified && (now - inverterDownSinceMs) >= 15000 &&
            (lastOfflineNoticeMs == 0 || (now - lastOfflineNoticeMs) >= 300000))
        {
            inverterOfflineNotified = true;
            lastOfflineNoticeMs = now;
            requestLoud("\xF0\x9F\x94\xB4 <b>Inverter offline</b>"); // 🔴
        }
    }
    bool alertArmed[kBatteryAlertCount] = {true, true, true, true, true};
    uint32_t lowSinceMs[kBatteryAlertCount] = {0, 0, 0, 0, 0};

    void queueAlert(const String &text)
    {
        lockTake();
        if (pendingAlerts.size() < 4)
        {
            pendingAlerts.push_back(text);
        }
        lockGive();
    }

    std::vector<String> chatList()
    {
        lockTake();
        std::vector<String> copy = chatIds;
        lockGive();
        return copy;
    }

    bool isKnownChat(const String &id)
    {
        for (const String &c : chatList())
        {
            if (c == id) return true;
        }
        return false;
    }

    ChatState &stateFor(const String &id)
    {
        for (ChatState &c : chats)
        {
            if (c.id == id) return c;
        }
        ChatState fresh;
        fresh.id = id;
        chats.push_back(fresh);
        return chats.back();
    }

    void broadcastSummary(bool silent, const String &headline, bool edit = false)
    {
        for (const String &id : chatList())
        {
            sendSummary(id, 0, String(), silent, headline, edit);
        }
    }

    // Main thread: battery threshold alerts (30/25/20/15/10 %). A level has to stay crossed for kBatteryConfirmMs before
    // it alerts, so a single bad frame can never trigger it, and a drop through several levels at once sends one message
    // for the lowest level. Each level re-arms once the battery is 3 points above it.
    void checkBatteryAlerts(bool inverterConnected)
    {
        if (!_settings.get.telegramBatteryAlerts() || !inverterConnected)
        {
            lastBatteryPercent = -1;
            for (size_t i = 0; i < kBatteryAlertCount; ++i)
            {
                lowSinceMs[i] = 0;
            }
            return;
        }
        float value = 0;
        if (!readNumber(DESCR_Battery_Percent, value))
        {
            return;
        }
        const int current = constrain(static_cast<int>(value + 0.5f), 0, 100);
        const uint32_t now = millis();
        if (lastBatteryPercent < 0)
        {
            lastBatteryPercent = current;
            for (size_t i = 0; i < kBatteryAlertCount; ++i)
            {
                alertArmed[i] = current > kBatteryAlertLevels[i];
                lowSinceMs[i] = 0;
            }
            return;
        }
        int fireLevel = -1;
        for (size_t i = 0; i < kBatteryAlertCount; ++i)
        {
            const int level = kBatteryAlertLevels[i];
            if (current >= level + kBatteryRearmMargin)
            {
                alertArmed[i] = true;
            }
            if (current > level)
            {
                lowSinceMs[i] = 0;
                continue;
            }
            if (lowSinceMs[i] == 0)
            {
                lowSinceMs[i] = now | 1u; // non-zero while the level is crossed
            }
            if (alertArmed[i] && (now - lowSinceMs[i]) >= kBatteryConfirmMs)
            {
                alertArmed[i] = false;
                fireLevel = level; // levels are descending, so the lowest crossed level wins
            }
        }
        lastBatteryPercent = current;
        if (fireLevel < 0)
        {
            return;
        }
        String text = "\xF0\x9F\x94\x8B <b>Battery " + String(current) + "%</b>, below " + String(fireLevel) + "%\n"; // 🔋
        const String mode = htmlEscape(readText(DESCR_Inverter_Operation_Mode));
        text += "Mode: " + (mode.length() ? mode : String("?")) + "  Load " + num(DESCR_AC_Out_Watt, 0, " W") + "  " +
                num(DESCR_Battery_Voltage, 1, " V");
        queueAlert(text);
    }

    // Main thread: one loud summary when the load rises above 80 %, re-armed below 70 %.
    void checkLoadAlert(bool inverterConnected)
    {
        if (!_settings.get.telegramLoadAlert() || !inverterConnected)
        {
            loadAlertArmed = true;
            return;
        }
        float value = 0;
        if (!readNumber(DESCR_AC_Out_Percent, value))
        {
            return;
        }
        const int current = static_cast<int>(value + 0.5f);
        if (loadAlertArmed && current > kLoadAlertOnPercent)
        {
            loadAlertArmed = false;
            requestLoud("\xF0\x9F\x94\x94 <b>High load</b>"); // 🔔
        }
        else if (!loadAlertArmed && current < kLoadAlertOffPercent)
        {
            loadAlertArmed = true;
        }
    }

    // Task: deliver one queued alert; returns true when something was sent.
    bool sendPendingAlert()
    {
        String alert;
        lockTake();
        if (!pendingAlerts.empty())
        {
            alert = pendingAlerts.front();
            pendingAlerts.erase(pendingAlerts.begin());
        }
        lockGive();
        if (alert.length() == 0)
        {
            return false;
        }
        const std::vector<String> targets = chatList();
        if (targets.empty())
        {
            setError("Battery alert dropped: no chat id configured");
            return true;
        }
        for (const String &chat : targets)
        {
            JsonDocument markup;
            buildSummaryMarkup(markup, chat, !dashDisabled.load(), false); // Dashboard button, no Upgrade on alerts
            if (sendText(chat, alert, &markup, nullptr))
            {
                taskLog("[Telegram] Battery alert sent to " + chat);
            }
        }
        return true;
    }

    // LogSerial forwards to WebSerial (async web socket) and must only be used from the main thread,
    // so the task queues its lines and loop() emits them.
    void taskLog(const String &line)
    {
        lockTake();
        if (pendingLogs.size() < 8)
        {
            pendingLogs.push_back(line);
        }
        lockGive();
    }

    void flushLogs()
    {
        std::vector<String> lines;
        lockTake();
        lines.swap(pendingLogs);
        lockGive();
        for (const String &line : lines)
        {
            LogSerial.println(line);
        }
    }

    void lockTake() { xSemaphoreTake(lock, portMAX_DELAY); }
    void lockGive() { xSemaphoreGive(lock); }

    void loadSettings()
    {
        lockTake();
        enabled = _settings.get.telegramEnabled();
        autoSummary = _settings.get.telegramAutoSummary();
        token = _settings.get.telegramToken();
        chatIdsRaw = _settings.get.telegramChatId();
        token.trim();
        chatIds.clear();
        String item;
        for (size_t i = 0; i <= chatIdsRaw.length(); ++i)
        {
            const char c = i < chatIdsRaw.length() ? chatIdsRaw[i] : ',';
            if (c == ',' || c == ';' || c == ' ' || c == '\n' || c == '\t')
            {
                item.trim();
                if (item.length()) chatIds.push_back(item);
                item = "";
            }
            else
            {
                item += c;
            }
        }
        lockGive();
    }

    void setError(const String &message)
    {
        lockTake();
        lastError = message;
        lockGive();
        if (message.length())
        {
            taskLog("[Telegram] " + message);
        }
    }

    // POST <method> with a JSON body; fills `out` with the parsed response. Reuses the TLS connection.
    bool api(const char *method, const JsonDocument &body, JsonDocument &out, uint32_t timeoutMs)
    {
        String payload;
        serializeJson(body, payload);
        return post(method, "application/json", reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length(), out, timeoutMs);
    }

    // sendDocument with a text file (multipart upload), for /log and crash reports.
    bool sendDocument(const String &chat, const String &fileName, const String &content, const String &caption)
    {
        static const char kBoundary[] = "solar2mqtt-4f1c9a7e";
        String body;
        body.reserve(content.length() + caption.length() + 400);
        auto field = [&](const char *name, const String &value) {
            body += String("--") + kBoundary + "\r\nContent-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
        };
        field("chat_id", chat);
        if (caption.length())
        {
            field("caption", caption);
            field("parse_mode", "HTML");
        }
        body += String("--") + kBoundary + "\r\nContent-Disposition: form-data; name=\"document\"; filename=\"" + fileName +
                "\"\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n";
        body += content;
        body += String("\r\n--") + kBoundary + "--\r\n";
        JsonDocument out;
        return post("sendDocument", String("multipart/form-data; boundary=") + kBoundary,
                    reinterpret_cast<const uint8_t *>(body.c_str()), body.length(), out, kSendHttpTimeoutMs);
    }

    bool post(const char *method, const String &contentType, const uint8_t *payload, size_t length, JsonDocument &out, uint32_t timeoutMs)
    {
        String tokenCopy;
        lockTake();
        tokenCopy = token;
        lockGive();

        String url = String(kApiHost) + tokenCopy + "/" + method;
        http.setReuse(true);
        http.setTimeout(timeoutMs);
        http.useHTTP10(false);
        if (!http.begin(client, url))
        {
            setError(String("HTTP begin failed for ") + method);
            return false;
        }
        http.addHeader("Content-Type", contentType);
        const int code = http.POST(const_cast<uint8_t *>(payload), length);
        if (code <= 0)
        {
            setError(String(method) + ": " + http.errorToString(code));
            http.end();
            return false;
        }
        const String response = http.getString();
        http.end();

        const DeserializationError err = deserializeJson(out, response);
        if (err)
        {
            setError(String(method) + ": bad JSON (" + err.c_str() + ")");
            return false;
        }
        if (!out["ok"].as<bool>())
        {
            const String description = out["description"] | "unknown error";
            setError(String(method) + ": " + description);
            return false;
        }
        return true;
    }

    bool identify()
    {
        JsonDocument body;
        body.to<JsonObject>();
        JsonDocument out;
        if (!api("getMe", body, out, kSendHttpTimeoutMs))
        {
            return false;
        }
        lockTake();
        botUsername = out["result"]["username"] | "";
        lastError = "";
        lockGive();

        JsonDocument commands;
        JsonArray list = commands["commands"].to<JsonArray>();
        JsonObject summary = list.add<JsonObject>();
        summary["command"] = "summary";
        summary["description"] = "Inverter summary";
        JsonObject start = list.add<JsonObject>();
        start["command"] = "start";
        start["description"] = "Show the Refresh button";
        JsonObject restart = list.add<JsonObject>();
        restart["command"] = "restart";
        restart["description"] = "Reboot the Solar2MQTT board";
        JsonObject upgrade = list.add<JsonObject>();
        upgrade["command"] = "upgrade";
        upgrade["description"] = "Install new firmware if available";
        JsonObject diag = list.add<JsonObject>();
        diag["command"] = "diag";
        diag["description"] = "Board diagnostics";
        JsonObject logCommand = list.add<JsonObject>();
        logCommand["command"] = "log";
        logCommand["description"] = "Recent board log as a file";
        JsonDocument ignore;
        api("setMyCommands", commands, ignore, kSendHttpTimeoutMs);

        taskLog("[Telegram] Bot @" + botUsername + " ready");
        return true;
    }

    void persistState()
    {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        for (const ChatState &c : chats)
        {
            if (c.lastMsgId != 0) obj[c.id] = c.lastMsgId;
        }
        String json;
        serializeJson(doc, json);
        prefs.putString("lastMsgs", json);
        prefs.putLong64("offset", updateOffset);
    }

    void loadState()
    {
        updateOffset = prefs.getLong64("offset", 0);
        chats.clear();
        const String json = prefs.getString("lastMsgs", "");
        JsonDocument doc;
        if (json.length() && !deserializeJson(doc, json))
        {
            for (JsonPair kv : doc.as<JsonObject>())
            {
                ChatState c;
                c.id = kv.key().c_str();
                c.lastMsgId = kv.value().as<long long>();
                chats.push_back(c);
            }
            return;
        }
        // Migration from the single-chat key.
        const int64_t legacy = prefs.getLong64("lastMsg", 0);
        const std::vector<String> ids = chatList();
        if (legacy != 0 && ids.size() == 1)
        {
            ChatState c;
            c.id = ids[0];
            c.lastMsgId = legacy;
            chats.push_back(c);
        }
    }

    // Newest release the updater has seen (empty when up to date). Kept through a failed download or check, so the
    // Upgrade button stays for a retry.
    String newVersionOffered()
    {
        if (updater == nullptr)
        {
            return String();
        }
        const GitHubOtaUpdater::State st = updater->state();
        if (st == GitHubOtaUpdater::State::UpdateAvailable)
        {
            offeredVersion = updater->latestVersion();
        }
        else if (st == GitHubOtaUpdater::State::UpToDate)
        {
            offeredVersion = "";
        }
        return offeredVersion;
    }

    String snapshotWithFooter()
    {
        String text;
        uint32_t age = 0;
        lockTake();
        text = summarySnapshot;
        age = snapshotMs ? (millis() - snapshotMs) / 1000 : 0;
        lockGive();
        if (text.length() == 0)
        {
            text = "\xE2\x9A\xA0\xEF\xB8\x8F No inverter data yet"; // ⚠️
        }
        text += "\n<i>\xF0\x9F\x95\x92 Updated: " + String(age) + "s ago</i>"; // 🕒
        text += "\n<i>\xF0\x9F\x92\xBE Version: " + runningVersion() + "</i>"; // 💾
        const String offered = newVersionOffered();
        if (offered.length())
        {
            text += "\n\xF0\x9F\x86\x95 <b>New version " + offered + " available</b>"; // 🆕
        }
        return text;
    }

    void deleteMessage(const String &chat, int64_t messageId)
    {
        if (messageId == 0)
        {
            return;
        }
        JsonDocument body;
        body["chat_id"] = chat;
        body["message_id"] = messageId;
        JsonDocument out;
        api("deleteMessage", body, out, kSendHttpTimeoutMs); // failures (older than 48 h, already gone) are fine
    }

    void answerCallback(const String &callbackId, const char *text)
    {
        if (callbackId.length() == 0)
        {
            return;
        }
        JsonDocument body;
        body["callback_query_id"] = callbackId;
        if (text && *text)
        {
            body["text"] = text;
        }
        JsonDocument out;
        api("answerCallbackQuery", body, out, kSendHttpTimeoutMs);
    }

    bool sendText(const String &chat, const String &text, JsonDocument *replyMarkup, int64_t *messageIdOut, bool silent = false)
    {
        JsonDocument body;
        body["chat_id"] = chat;
        body["text"] = text;
        body["parse_mode"] = "HTML";
        body["disable_web_page_preview"] = true;
        if (silent)
        {
            body["disable_notification"] = true;
        }
        if (replyMarkup)
        {
            body["reply_markup"] = replyMarkup->as<JsonVariantConst>();
        }
        JsonDocument out;
        if (!api("sendMessage", body, out, kSendHttpTimeoutMs))
        {
            return false;
        }
        if (messageIdOut)
        {
            *messageIdOut = out["result"]["message_id"].as<long long>();
        }
        return true;
    }

    // The summary's only button is Dashboard (the summary refreshes itself); Refresh appears only when there is no
    // dashboard link. Upgrade goes on a second row when a newer release is out.
    void buildSummaryMarkup(JsonDocument &markup, const String &chat, bool withDashboard, bool withUpgrade = true)
    {
        JsonArray rows = markup["inline_keyboard"].to<JsonArray>();
        JsonArray first = rows.add<JsonArray>();
        String dash;
        if (withDashboard)
        {
            lockTake();
            dash = dashboardUrl;
            lockGive();
        }
        if (dash.length())
        {
            JsonObject dashboard = first.add<JsonObject>();
            dashboard["text"] = "\xF0\x9F\x93\x8A Dashboard"; // 📊
            if (chat.startsWith("-"))
            {
                dashboard["url"] = dash; // Mini App buttons only work in private chats; groups get a browser link
            }
            else
            {
                dashboard["web_app"]["url"] = dash;
            }
        }
        else
        {
            JsonObject button = first.add<JsonObject>();
            button["text"] = "\xF0\x9F\x94\x84 Refresh"; // 🔄
            button["callback_data"] = "summary";
        }
        if (withUpgrade && newVersionOffered().length())
        {
            JsonObject upgradeButton = rows.add<JsonArray>().add<JsonObject>(); // second row, under Refresh
            upgradeButton["text"] = "\xE2\xAC\x86\xEF\xB8\x8F Upgrade"; // ⬆️
            upgradeButton["callback_data"] = "upgrade";
        }
    }

    // Telegram refused the summary because of the Dashboard button (link too long or not allowed): shorten the history
    // in the next link, or drop the button when even a link without history is refused.
    bool dashboardRejected()
    {
        String err;
        lockTake();
        err = lastError;
        lockGive();
        String upper = err;
        upper.toUpperCase();
        if (upper.indexOf("BUTTON") < 0 && upper.indexOf("URL") < 0 && upper.indexOf("WEB_APP") < 0)
        {
            return false;
        }
        const uint8_t slots = dashSlotsAllowed.load();
        if (slots == 0)
        {
            dashDisabled = true;
        }
        else
        {
            dashSlotsAllowed = slots > 12 ? slots / 2 : 0;
        }
        taskLog("[Telegram] Dashboard link rejected (" + err + "); history now " + String(dashSlotsAllowed.load()) + " slots" +
                (dashDisabled.load() ? ", button off" : ""));
        return true;
    }

    // editMessageText for a summary; "message is not modified" (nothing changed since the last edit) counts as success.
    bool editText(const String &chat, int64_t messageId, const String &text, JsonDocument *replyMarkup)
    {
        JsonDocument body;
        body["chat_id"] = chat;
        body["message_id"] = messageId;
        body["text"] = text;
        body["parse_mode"] = "HTML";
        body["disable_web_page_preview"] = true;
        if (replyMarkup)
        {
            body["reply_markup"] = replyMarkup->as<JsonVariantConst>();
        }
        JsonDocument out;
        if (api("editMessageText", body, out, kSendHttpTimeoutMs))
        {
            return true;
        }
        const String description = out["description"] | "";
        if (description.indexOf("not modified") >= 0)
        {
            setError(""); // not an error: the summary was already up to date
            return true;
        }
        return false;
    }

    // edit: update the last summary in place (automatic summaries, the Refresh button, after a restart) instead of
    // sending a new one; falls back to a new message when there is none or it can no longer be edited.
    void sendSummary(const String &chat, int64_t triggerMessageId, const String &callbackId, bool silent = false,
                     const String &headline = String(), bool edit = false)
    {
        // Right after boot the main loop may not have built a snapshot yet; give it a few seconds.
        for (int i = 0; i < 20; ++i)
        {
            lockTake();
            const bool have = snapshotMs != 0;
            lockGive();
            if (have) break;
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        const uint32_t now = millis();
        ChatState &state = stateFor(chat);
        if (state.lastSummaryMs && (now - state.lastSummaryMs) < kMinSummaryGapMs)
        {
            answerCallback(callbackId, "Please wait a moment");
            return;
        }

        String body = snapshotWithFooter();
        if (headline.length())
        {
            body = headline + "\n" + body;
        }
        int64_t newId = 0;
        bool edited = false;
        bool sent = false;
        bool withDashboard = !dashDisabled.load();
        for (int attempt = 0; attempt < 2 && !sent; ++attempt)
        {
            JsonDocument markup;
            buildSummaryMarkup(markup, chat, withDashboard);
            if (edit && state.lastMsgId != 0 && editText(chat, state.lastMsgId, body, &markup))
            {
                newId = state.lastMsgId;
                edited = true;
                sent = true;
            }
            else if (sendText(chat, body, &markup, &newId, silent))
            {
                sent = true;
            }
            else if (withDashboard && dashboardRejected())
            {
                withDashboard = false; // this summary goes out without the button; the next link is shorter
            }
            else
            {
                break;
            }
        }
        if (!sent)
        {
            answerCallback(callbackId, "Failed to send summary");
            return;
        }
        answerCallback(callbackId, nullptr);
        state.lastSummaryMs = now;
        lastSummaryMs = now;
        ++summariesSent;
        taskLog("[Telegram] Summary sent (" + String(edited ? "edited " : "") + "msg " + String(static_cast<long long>(newId)) + "); task stack free " +
                String(static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr))) + " B, heap " +
                String(static_cast<unsigned>(ESP.getFreeHeap())) + " B (largest " + String(static_cast<unsigned>(ESP.getMaxAllocHeap())) + " B)");

        const int64_t previous = state.lastMsgId;
        state.lastMsgId = newId;
        if (previous != 0 && previous != newId)
        {
            deleteMessage(chat, previous);
        }
        removeTriggerMessage(chat, triggerMessageId);
        if (!edited)
        {
            persistState(); // an edit keeps the same message id, so there is nothing new to store
        }
    }

    // Older firmware showed a persistent "Refresh" keyboard, which stays in the chat until a message removes it: send
    // one with remove_keyboard and delete it again, once per board.
    void removeOldKeyboardOnce()
    {
        const std::vector<String> ids = chatList();
        if (ids.empty() || prefs.isKey("kbGone"))
        {
            return;
        }
        for (const String &id : ids)
        {
            JsonDocument markup;
            markup["remove_keyboard"] = true;
            int64_t messageId = 0;
            if (sendText(id, "Keyboard removed", &markup, &messageId, true) && messageId != 0)
            {
                deleteMessage(id, messageId);
            }
        }
        prefs.putBool("kbGone", true);
        taskLog("[Telegram] Old Refresh keyboard removed");
    }

    void sendWelcome(const String &chat)
    {
        JsonDocument markup;
        markup["remove_keyboard"] = true; // older firmware had a persistent Refresh keyboard
        sendText(chat,
                 "<b>Solar2MQTT</b> connected.\nThe summary below keeps itself up to date; tap <b>Dashboard</b> for charts "
                 "and details. Send /summary for a fresh one at the bottom, /restart to reboot the board, /diag or /log for "
                 "troubleshooting.",
                 &markup, nullptr);
    }

    String runningVersion() const
    {
        return updater != nullptr ? String(updater->currentVersion()) : String(STRVERSION);
    }

    // Upgrade button or /upgrade, step 1 (bot task): ask the updater for the newest release. No chat messages: the
    // summary is the status (after the restart it shows the new version and the button is gone). The updater pauses
    // this connection while it talks to GitHub; progressUpgrade() picks up the result once the bot is back.
    bool startUpgrade(const String &chat)
    {
        if (updater == nullptr || upgradeStage.load() != 0 || updater->isBusy())
        {
            return false;
        }
        upgradeChat = chat;
        upgradeStage = 1;
        if (!updater->requestCheck())
        {
            upgradeStage = 0;
            return false;
        }
        return true;
    }

    // Steps 2 and 3 (bot task). Returns true when it has just handed the network to the updater. When it ends without
    // an install, the summary is re-posted quietly: same version, and after a failure the Upgrade button again.
    bool progressUpgrade()
    {
        const uint8_t stage = upgradeStage.load();
        if (stage == 0 || updater == nullptr || updater->isBusy())
        {
            return false;
        }
        const GitHubOtaUpdater::State st = updater->state();
        if (stage == 2 && st == GitHubOtaUpdater::State::Success)
        {
            return false; // restart pending
        }
        upgradeStage = 0;
        if (stage == 1 && st == GitHubOtaUpdater::State::UpdateAvailable)
        {
            const String latest = updater->latestVersion();
            prefs.putString("upgVer", latest); // the new firmware logs the result and posts a fresh summary
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (updater->startUpdate())
                {
                    upgradeStage = 2;
                    taskLog("[Telegram] Upgrade: installing " + latest + " (running " + runningVersion() + ")");
                    return true;
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            prefs.remove("upgVer");
            taskLog("[Telegram] Upgrade: could not start the download");
        }
        else if (stage == 1 && st == GitHubOtaUpdater::State::UpToDate)
        {
            taskLog("[Telegram] Upgrade: already on the latest version " + runningVersion());
        }
        else
        {
            if (stage == 2)
            {
                prefs.remove("upgVer");
            }
            taskLog(String("[Telegram] Upgrade: ") + (stage == 1 ? "check" : "download") + " failed: " + updater->lastError());
        }
        sendSummary(upgradeChat, 0, String(), true, String(), true);
        return false;
    }

    // Once per boot, when the bot first connects: log the result of an upgrade and schedule the first summary
    // (after an upgrade, or when automatic summaries are on). It goes out once the inverter has reported.
    void handleFirstReady()
    {
        if (firstReadyHandled)
        {
            return;
        }
        firstReadyHandled = true;
        const String expected = prefs.isKey("upgVer") ? prefs.getString("upgVer", "") : String();
        if (prefs.isKey("upgChat"))
        {
            prefs.remove("upgChat"); // written by 2.1.5 and older
        }
        if (expected.length())
        {
            prefs.remove("upgVer");
            const String running = runningVersion();
            taskLog(running == expected ? "[Telegram] Upgrade: now running " + running
                                        : "[Telegram] Upgrade: the update to " + expected + " did not complete, running " + running);
        }
        bool autoOn;
        lockTake();
        autoOn = autoSummary;
        lockGive();
        bootSummaryPending = autoOn || expected.length() > 0;
        removeOldKeyboardOnce();
    }

    // The user's own command message (/summary, Refresh, /diag, ...) is always removed, so the chat only shows the bot's
    // messages.
    void removeTriggerMessage(const String &chat, int64_t messageId)
    {
        if (messageId != 0)
        {
            deleteMessage(chat, messageId);
        }
    }

    // /diag: the main thread builds the text (it owns the shared state); give it up to 3 s.
    void sendDiag(const String &chat)
    {
        diagReady = false;
        diagRequested = true;
        for (int i = 0; i < 60 && !diagReady.load(); ++i)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        String text;
        if (diagReady.load())
        {
            lockTake();
            text = diagText;
            diagText = "";
            lockGive();
        }
        else
        {
            diagRequested = false;
            text = "\xF0\x9F\xA9\xBA <b>Diagnostics</b>\nThe main loop did not answer within 3 s, it may be stuck. Up " +
                   DiagLog::formatDuration(millis() / 1000) + ", " + String(ESP.getFreeHeap() / 1024) + " KB free.";
        }
        sendText(chat, text, nullptr, nullptr);
        taskLog("[Telegram] Diagnostics sent");
    }

    // /log: the recent board log (the RTC ring buffer) as a text file.
    void sendLog(const String &chat)
    {
        const String log = DiagLog::recent(8192);
        if (log.length() == 0)
        {
            sendText(chat, "The log is empty.", nullptr, nullptr);
            return;
        }
        const String caption = "\xF0\x9F\x93\x9C Board log, firmware " + runningVersion() + ", up " + DiagLog::formatDuration(millis() / 1000); // 📜
        const bool ok = sendDocument(chat, "solar2mqtt-log.txt", log, caption);
        taskLog(ok ? "[Telegram] Log sent (" + String(log.length()) + " bytes)" : String("[Telegram] Sending the log failed"));
    }

    // After a crash, watchdog or brownout restart: tell every chat why, with the log from before the restart.
    void sendCrashReport()
    {
        if (!DiagLog::crashReportPending() || (lastCrashTryMs != 0 && millis() - lastCrashTryMs < 30000))
        {
            return;
        }
        const std::vector<String> ids = chatList();
        if (ids.empty() || crashReportTries >= 3)
        {
            DiagLog::crashReportSent();
            return;
        }
        ++crashReportTries;
        lastCrashTryMs = millis();
        const String &log = DiagLog::crashReportLog();
        const String caption = "\xF0\x9F\x92\xA5 <b>Unexpected restart</b>\n" + htmlEscape(DiagLog::crashReportText()) + "\nFirmware " +
                               runningVersion(); // 💥
        bool ok = true;
        for (const String &id : ids)
        {
            const bool sent = log.length() ? sendDocument(id, "solar2mqtt-crash-log.txt", log, caption + ". The log from before it is attached.")
                                           : sendText(id, caption, nullptr, nullptr);
            ok = ok && sent;
        }
        taskLog(String("[Telegram] Crash report ") + (ok ? "sent" : "failed") + "\n" + DiagLog::crashReportText());
        if (ok)
        {
            DiagLog::crashReportSent();
        }
    }

    static bool isSummaryText(String text)
    {
        text.trim();
        if (text.startsWith("/summary"))
        {
            return true;
        }
        text.toLowerCase();
        return text == "summary" || text.endsWith(" summary") || text == "refresh" || text.endsWith(" refresh") || text.startsWith("/refresh");
    }

    void handleUpdate(JsonVariantConst update)
    {
        const bool paired = !chatList().empty();

        JsonVariantConst message = update["message"];
        JsonVariantConst callback = update["callback_query"];

        String chat;
        int64_t messageId = 0;
        String text;
        String callbackId;
        String callbackData;

        if (!callback.isNull())
        {
            chat = String(callback["message"]["chat"]["id"].as<long long>());
            callbackId = callback["id"] | "";
            callbackData = callback["data"] | "";
        }
        else if (!message.isNull())
        {
            chat = String(message["chat"]["id"].as<long long>());
            messageId = message["message_id"].as<long long>();
            text = message["text"] | "";
        }
        else
        {
            return;
        }

        if (!paired)
        {
            // Not paired yet: tell the sender their chat id so it can be entered in the web UI.
            answerCallback(callbackId, nullptr);
            sendText(chat,
                     "This Solar2MQTT device is not paired yet.\nYour chat id is <code>" + chat +
                         "</code>. Enter it in the web UI under Telegram settings, then send /start.",
                     nullptr, nullptr);
            return;
        }
        if (!isKnownChat(chat))
        {
            taskLog("[Telegram] Ignoring update from chat " + chat);
            answerCallback(callbackId, nullptr);
            return;
        }

        if (!callback.isNull())
        {
            if (callbackData == "summary")
            {
                sendSummary(chat, 0, callbackId, true, String(), true); // updates the summary the button belongs to
            }
            else if (callbackData == "upgrade")
            {
                taskLog("[Telegram] Upgrade button pressed in chat " + chat);
                const String target = newVersionOffered();
                if (startUpgrade(chat))
                {
                    // A short pop-up on the button, not a chat message.
                    const String toast = "\xE2\xAC\x86\xEF\xB8\x8F Installing " + (target.length() ? target : String("the new version")) +
                                         ", the board restarts when done"; // ⬆️
                    answerCallback(callbackId, toast.c_str());
                }
                else
                {
                    answerCallback(callbackId, "An upgrade is already running");
                }
            }
            else
            {
                answerCallback(callbackId, nullptr);
            }
            return;
        }

        String command = text;
        command.trim();
        if (command.startsWith("/start") || command.startsWith("/help"))
        {
            sendWelcome(chat);
            sendSummary(chat, 0, String());
            removeTriggerMessage(chat, messageId);
            return;
        }
        if (command.startsWith("/diag"))
        {
            taskLog("[Telegram] Diagnostics requested from chat " + chat);
            removeTriggerMessage(chat, messageId);
            sendDiag(chat);
            return;
        }
        if (command.startsWith("/log"))
        {
            taskLog("[Telegram] Log requested from chat " + chat);
            removeTriggerMessage(chat, messageId);
            sendLog(chat);
            return;
        }
        if (command.startsWith("/upgrade"))
        {
            taskLog("[Telegram] Upgrade requested from chat " + chat);
            removeTriggerMessage(chat, messageId);
            startUpgrade(chat); // silent like the button; progressUpgrade() re-posts the summary if nothing is installed
            return;
        }
        if (command.startsWith("/restart") || command.equalsIgnoreCase("restart"))
        {
            taskLog("[Telegram] Restart requested from chat " + chat);
            sendText(chat, "\xF0\x9F\x94\x84 <b>Restarting</b>\nThe board will be back in about 15 seconds.", nullptr, nullptr); // 🔄
            removeTriggerMessage(chat, messageId);
            persistState(); // keep the summary message id so cleanup still works after the reboot
            g_restartAt = millis() + 1500;
            g_pendingRestart = true;
            return;
        }
        if (isSummaryText(command))
        {
            sendSummary(chat, messageId, String());
            return;
        }
        // Anything else: quietly ignore, keep the chat clean.
    }

    bool pollUpdates(uint32_t timeoutSeconds)
    {
        JsonDocument body;
        body["offset"] = updateOffset;
        body["limit"] = 1;
        body["timeout"] = timeoutSeconds;
        JsonArray allowed = body["allowed_updates"].to<JsonArray>();
        allowed.add("message");
        allowed.add("callback_query");

        JsonDocument out;
        if (!api("getUpdates", body, out, (timeoutSeconds + 15) * 1000))
        {
            return false;
        }
        JsonArrayConst results = out["result"].as<JsonArrayConst>();
        for (JsonVariantConst update : results)
        {
            const int64_t id = update["update_id"].as<long long>();
            if (id >= updateOffset)
            {
                updateOffset = id + 1;
            }
            handleUpdate(update);
        }
        if (results.size() > 0)
        {
            persistState();
        }
        return true;
    }

    void taskLoop()
    {
        prefs.begin(kPrefsNamespace, false);
        loadState();
        client.setCACert(TELEGRAM_ROOT_CA);
        client.setHandshakeTimeout(15);

        for (;;)
        {
            if (configChanged.exchange(false))
            {
                http.end();
                client.stop();
                ready = false;
                loadSettings();
            }

            if (pauseRequested.load())
            {
                if (!pausedAck.load())
                {
                    http.end();
                    client.stop(); // free the TLS session for the firmware updater
                    ready = false;
                    pausedAck = true;
                    taskLog("[Telegram] Paused for a firmware update check");
                }
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            if (pausedAck.exchange(false))
            {
                taskLog("[Telegram] Resuming");
            }

            bool active;
            lockTake();
            active = enabled && token.length() > 0;
            lockGive();
            if (!active || !networkConnected || !networkConnected())
            {
                ready = false;
                vTaskDelay(pdMS_TO_TICKS(kIdleDelayMs));
                continue;
            }

            if (!ready)
            {
                if (!identify())
                {
                    vTaskDelay(pdMS_TO_TICKS(kErrorBackoffMs));
                    continue;
                }
                ready = true;
            }

            handleFirstReady();
            sendCrashReport();
            if (progressUpgrade())
            {
                continue; // hand the connection to the updater now instead of starting a long poll
            }
            if (updater != nullptr && updater->isBusy())
            {
                // The updater's task is about to pause this connection; a 20 s long poll now would hold it up (it did
                // delay the download after the Upgrade button by 19 s).
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            if (summaryRequested.exchange(false))
            {
                if (!chatList().empty())
                {
                    broadcastSummary(false, String());
                }
                else
                {
                    setError("No chat id configured");
                }
            }

            if (loudSummaryRequested.load())
            {
                const uint32_t sinceLast = millis() - lastSummaryMs;
                if (lastSummaryMs != 0 && sinceLast < kMinSummaryGapMs)
                {
                    vTaskDelay(pdMS_TO_TICKS(kMinSummaryGapMs - sinceLast));
                }
                loudSummaryRequested = false;
                String headline;
                lockTake();
                headline = loudHeadline;
                loudHeadline = "";
                lockGive();
                broadcastSummary(false, headline);
                continue;
            }

            if (sendPendingAlert())
            {
                continue;
            }

            // Automatic summary: send when due, otherwise shorten the long poll so the next one lands on time.
            uint32_t pollTimeout = kLongPollSeconds;
            if (bootSummaryPending)
            {
                // First summary after a restart: give the inverter time to report instead of posting blank values.
                const uint32_t up = millis();
                if (up >= kBootSummaryDelayMs)
                {
                    bootSummaryPending = false;
                    lastAutoSummaryMs = up;
                    broadcastSummary(true, String(), true);
                    continue;
                }
                const uint32_t remainingSec = (kBootSummaryDelayMs - up + 999) / 1000;
                if (remainingSec < pollTimeout)
                {
                    pollTimeout = remainingSec > 0 ? remainingSec : 1;
                }
            }
            else
            {
                bool autoOn;
                lockTake();
                autoOn = autoSummary;
                lockGive();
                // None while an upgrade runs: the summary with the Upgrade button stays until the new firmware replaces
                // it, so the version line and the button change together.
                if (autoOn && !chatList().empty() && upgradeStage.load() == 0)
                {
                    const uint32_t since = millis() - lastAutoSummaryMs;
                    if (lastAutoSummaryMs == 0 || since >= kAutoSummaryIntervalMs)
                    {
                        lastAutoSummaryMs = millis();
                        broadcastSummary(true, String(), true);
                        continue;
                    }
                    const uint32_t remainingSec = (kAutoSummaryIntervalMs - since + 999) / 1000;
                    if (remainingSec < pollTimeout)
                    {
                        pollTimeout = remainingSec > 0 ? remainingSec : 1;
                    }
                }
            }

            if (!pollUpdates(pollTimeout))
            {
                vTaskDelay(pdMS_TO_TICKS(kErrorBackoffMs));
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    static void taskEntry(void *arg)
    {
        static_cast<Impl *>(arg)->taskLoop();
        vTaskDelete(nullptr);
    }

    String updateStatusText()
    {
        if (updater == nullptr)
        {
            return String("not available");
        }
        switch (updater->state())
        {
        case GitHubOtaUpdater::State::Idle:
            return String("not checked yet");
        case GitHubOtaUpdater::State::Checking:
            return String("checking");
        case GitHubOtaUpdater::State::UpToDate:
            return String("up to date");
        case GitHubOtaUpdater::State::UpdateAvailable:
            return "version " + updater->latestVersion() + " available";
        case GitHubOtaUpdater::State::Downloading:
            return String("downloading");
        case GitHubOtaUpdater::State::Success:
            return String("installed, restart pending");
        case GitHubOtaUpdater::State::Error:
            return "last check failed: " + htmlEscape(updater->lastError());
        }
        return String("?");
    }

    // Main thread: the /diag text. It reads the shared state document, so it must not run on the bot task.
    String buildDiag(bool inverterConnected)
    {
        JsonObjectConst esp = g_stateDoc["EspData"].as<JsonObjectConst>();
        JsonObjectConst status = g_stateDoc["Status"].as<JsonObjectConst>();
        const esp_partition_t *partition = esp_ota_get_running_partition();
        String t;
        t.reserve(1024);
        t += "\xF0\x9F\xA9\xBA <b>Diagnostics</b>\n"; // 🩺
        t += "Firmware " + runningVersion();
        if (partition != nullptr)
        {
            t += String(" (") + partition->label + ")";
        }
        t += ", up " + DiagLog::formatDuration(millis() / 1000) + "\n";
        t += "Last restart: " + String(DiagLog::resetReasonText()) + ", boot " + String(DiagLog::bootsSincePowerOn()) + " since power-on";
        if (DiagLog::crashesSincePowerOn() > 0)
        {
            t += " (" + String(DiagLog::crashesSincePowerOn()) + " unexpected)";
        }
        t += "\n";
        t += "Wi-Fi: " + htmlEscape(WiFi.SSID()) + ", " + String(WiFi.RSSI()) + " dBm, channel " + String(WiFi.channel()) + ", " +
             WiFi.localIP().toString() + ", " + String(DiagLog::wifiDrops()) + " drops";
        const String drop = DiagLog::lastWifiDrop();
        if (drop.length())
        {
            t += " (last: " + htmlEscape(drop) + ")";
        }
        t += "\n";
        t += "Memory: " + String(ESP.getFreeHeap() / 1024) + " KB free, largest block " + String(ESP.getMaxAllocHeap() / 1024) +
             " KB, lowest " + String(ESP.getMinFreeHeap() / 1024) + " KB\n";
        t += "Inverter: " + String(inverterConnected ? "connected" : "<b>not connected</b>") + ", " +
             htmlEscape(String(status["protocol"] | "?")) + "\n";
        t += "  replies " + String(esp["PI_Ok"] | 0UL) + ", no answer " + String(esp["PI_NoAnswer"] | 0UL) + ", CRC errors " +
             String(esp["PI_CrcError"] | 0UL) + ", saved by retry " + String(esp["PI_RetrySaved"] | 0UL) + ", backoffs " +
             String(esp["PI_Backoffs"] | 0UL) + "\n";
        t += "  longest silence " + String((esp["PI_LongestSilenceMs"] | 0UL) / 1000.0f, 1) + " s, battery readings rejected " +
             String(esp["PI_BattRejected"] | 0UL) + "\n";
        String err;
        lockTake();
        err = lastError;
        lockGive();
        t += "Telegram: " + String(summariesSent) + " summaries sent, last error: " + (err.length() ? htmlEscape(err) : String("none")) + "\n";
        t += "Updates: " + updateStatusText() + "\n";
        const bool mqttConfigured = strlen(_settings.get.mqttHost()) > 0;
        t += "MQTT: " + String(!mqttConfigured ? "off" : ((esp["MQTTStatus"] | false) ? "connected" : "not connected"));
        return t;
    }

    // Main thread: estimated time until the inverter's low-battery cut-off while it runs on battery. The usable energy
    // above the reserve is drawn at load / efficiency + the inverter's own consumption (all from Device settings).
    // Empty when not on battery or the capacity is not set.
    String timeLeftText(const String &mode, float batteryPct)
    {
        const uint32_t capacityWh = _settings.get.batteryCapacityWh();
        String upper = mode;
        upper.toUpperCase();
        float loadW = 0;
        if (capacityWh == 0 || batteryPct < 0 || upper.indexOf("BATTERY") < 0 || !readNumber(DESCR_AC_Out_Watt, loadW))
        {
            return String();
        }
        const float reserve = _settings.get.batteryReservePct();
        const float efficiency = std::max<float>(_settings.get.inverterEfficiencyPct(), 50.0f) / 100.0f;
        const float drawW = std::max(loadW, 0.0f) / efficiency + _settings.get.inverterIdleW();
        if (drawW < 1.0f)
        {
            return String();
        }
        const float usableWh = capacityWh * std::max(batteryPct - reserve, 0.0f) / 100.0f;
        const uint32_t seconds = static_cast<uint32_t>(usableWh / drawW * 3600.0f);
        return "\xE2\x89\x88 " + DiagLog::formatDuration(seconds) + " until " + String(static_cast<int>(reserve)) + " %"; // ≈
    }

    void dashInit()
    {
#ifdef DASH_FAKE_HISTORY
        // Test builds only: a full day of made-up history, to check that Telegram accepts the longest Dashboard link.
        // It uses a different magic, so the next normal build discards it.
        if (dashHist.magic != kDashMagic + 1)
        {
            memset(&dashHist, 0, sizeof(dashHist));
            for (size_t i = 0; i < kDashSlots; ++i)
            {
                dashHist.batt[i] = static_cast<uint8_t>(60 + (i * 7) % 40);
                dashHist.load[i] = static_cast<uint8_t>(10 + (i * 13) % 50);
                dashHist.off[i] = static_cast<uint8_t>(i >= 70 && i < 90 ? 15 : 0);
            }
            dashHist.count = kDashSlots;
            dashHist.magic = kDashMagic + 1;
        }
        slotStartMs = millis();
        return;
#endif
        if (dashHist.magic != kDashMagic || dashHist.count > kDashSlots || dashHist.head >= kDashSlots)
        {
            memset(&dashHist, 0, sizeof(dashHist));
            dashHist.magic = kDashMagic;
        }
        slotStartMs = millis();
    }

    void dashPush(uint8_t batt, uint8_t load, uint8_t off)
    {
        dashHist.batt[dashHist.head] = batt;
        dashHist.load[dashHist.head] = load;
        dashHist.off[dashHist.head] = off;
        dashHist.head = (dashHist.head + 1) % kDashSlots;
        if (dashHist.count < kDashSlots)
        {
            ++dashHist.count;
        }
    }

    // Main thread, every snapshot (2 s): fold the live values into the current 15 minute slot and track how long the
    // grid has been out.
    void dashRecord(bool inverterConnected, bool gridOff, int batteryPct, float loadW)
    {
        const uint32_t now = millis();
        const int64_t unix = unixNow();
        if (inverterConnected)
        {
            slotLoadSum += loadW > 0 ? static_cast<uint32_t>(loadW) : 0;
            ++slotSamples;
            if (batteryPct >= 0)
            {
                slotBatt = batteryPct;
            }
            if (gridOff)
            {
                slotOffSeconds += kSnapshotIntervalMs / 1000;
                if (!gridWasOff)
                {
                    gridWasOff = true;
                    outageStartMs = now;
                    if (dashHist.outageStart == 0)
                    {
                        dashHist.outageStart = unix; // after a restart the RTC keeps the real start
                    }
                }
                else if (dashHist.outageStart == 0 && unix != 0)
                {
                    dashHist.outageStart = unix - (now - outageStartMs) / 1000; // the clock was set after the outage began
                }
            }
            else
            {
                gridWasOff = false;
                dashHist.outageStart = 0;
            }
        }
        if (now - slotStartMs < kDashSlotSeconds * 1000UL)
        {
            return;
        }
        if (unix != 0 && dashHist.lastSlotEnd != 0 && unix > dashHist.lastSlotEnd)
        {
            const int64_t missing = (unix - dashHist.lastSlotEnd) / kDashSlotSeconds - 1; // slots lost while the board was off
            for (int64_t i = 0; i < missing && i < static_cast<int64_t>(kDashSlots); ++i)
            {
                dashPush(kDashNone, kDashNone, kDashNone);
            }
        }
        if (slotSamples > 0)
        {
            dashPush(slotBatt >= 0 ? static_cast<uint8_t>(std::min(slotBatt, 100)) : kDashNone,
                     static_cast<uint8_t>(std::min<uint32_t>(slotLoadSum / slotSamples / 25, 254)),
                     static_cast<uint8_t>(std::min<uint32_t>((slotOffSeconds + 30) / 60, 15)));
        }
        else
        {
            dashPush(kDashNone, kDashNone, kDashNone);
        }
        dashHist.lastSlotEnd = unix;
        slotStartMs = now;
        slotLoadSum = 0;
        slotSamples = 0;
        slotOffSeconds = 0;
        slotBatt = -1;
    }

    // Main thread: the Dashboard link. Everything after '#' stays on the phone (browsers never send the fragment), so
    // the static page on GitHub Pages never sees the data.
    String buildDashboardUrl(bool inverterConnected, const String &mode, const String &alerts, bool solarConnected)
    {
        if (strlen(DASHBOARD_URL) == 0 || dashDisabled.load())
        {
            return String();
        }
        String u;
        u.reserve(1200);
        u += DASHBOARD_URL;
        u += "#v=1";
        auto add = [&u](const char *key, const String &value) {
            u += '&';
            u += key;
            u += '=';
            u += urlEncode(value);
        };
        auto addNumber = [&add](const char *key, const char *descr, float scale) {
            float value = 0;
            if (readNumber(descr, value))
            {
                add(key, String(lroundf(value * scale)));
            }
        };
        const int64_t unix = unixNow();
        add("t", String(static_cast<unsigned long>(unix)));
        add("c", inverterConnected ? "1" : "0");
        if (inverterConnected)
        {
            add("m", mode);
            addNumber("b", DESCR_Battery_Percent, 1);
            addNumber("bv", DESCR_Battery_Voltage, 10);
            addNumber("bc", DESCR_Battery_Charge_Current, 1);
            addNumber("bd", DESCR_Battery_Discharge_Current, 1);
            addNumber("l", DESCR_AC_Out_Watt, 1);
            addNumber("lp", DESCR_AC_Out_Percent, 1);
            addNumber("va", DESCR_AC_Out_VA, 1);
            addNumber("gv", DESCR_AC_In_Voltage, 10);
            addNumber("gf", DESCR_AC_In_Frequency, 10);
            addNumber("ov", DESCR_AC_Out_Voltage, 10);
            addNumber("of", DESCR_AC_Out_Frequency, 10);
            addNumber("tc", DESCR_Inverter_Bus_Temperature, 1);
            if (solarConnected)
            {
                addNumber("pv", DESCR_PV_Charging_Power, 1);
            }
            if (alerts.length())
            {
                add("w", alerts);
            }
        }
        const String rating = readStaticText(DESCR_AC_Out_Rating_Active_Power);
        if (rating.length()) add("lr", rating);
        const String outputPriority = readStaticText(DESCR_Output_Source_Priority);
        if (outputPriority.length()) add("op", outputPriority);
        const String chargerPriority = readStaticText(DESCR_Charger_Source_Priority);
        if (chargerPriority.length()) add("cp", chargerPriority);
        if (gridWasOff)
        {
            if (dashHist.outageStart != 0)
            {
                add("os", String(static_cast<unsigned long>(dashHist.outageStart)));
            }
            else
            {
                add("oa", String((millis() - outageStartMs) / 1000));
            }
        }
        const uint32_t batteryWh = _settings.get.batteryCapacityWh();
        if (batteryWh)
        {
            add("wh", String(batteryWh));
            add("wr", String(_settings.get.batteryReservePct()));
            add("wi", String(_settings.get.inverterIdleW()));
            add("we", String(_settings.get.inverterEfficiencyPct()));
        }
        JsonObjectConst esp = g_stateDoc["EspData"].as<JsonObjectConst>();
        add("ok", String(esp["PI_Ok"] | 0UL));
        add("na", String(esp["PI_NoAnswer"] | 0UL));
        add("rs", String(WiFi.RSSI()));
        add("fw", runningVersion());
        add("up", String(millis() / 1000));
        const size_t n = std::min<size_t>(dashHist.count, dashSlotsAllowed.load());
        if (n)
        {
            uint8_t bytes[3 * kDashSlots];
            const size_t first = (dashHist.head + kDashSlots - n) % kDashSlots;
            for (size_t i = 0; i < n; ++i)
            {
                const size_t k = (first + i) % kDashSlots;
                bytes[i] = dashHist.batt[k];
                bytes[n + i] = dashHist.load[k];
                bytes[2 * n + i] = dashHist.off[k];
            }
            add("s", String(kDashSlotSeconds));
            add("n", String(n));
            if (dashHist.lastSlotEnd != 0)
            {
                add("he", String(static_cast<unsigned long>(dashHist.lastSlotEnd)));
            }
            u += "&h=";
            u += base64Url(bytes, 3 * n);
        }
        return u;
    }

    // Main thread: build the summary text from live data.
    void buildSnapshot(bool inverterConnected, int rssi)
    {
        const bool solarConnected = _settings.get.solarConnected();
        String text;
        text.reserve(512);
        String modeRaw;
        String alerts;
        bool gridOff = false;
        int batteryPct = -1;
        float loadW = 0;
        if (!inverterConnected)
        {
            text += "\xE2\x9A\xA0\xEF\xB8\x8F Inverter not connected\n"; // ⚠️
        }
        else
        {
            const String mode = htmlEscape(readText(DESCR_Inverter_Operation_Mode));
            text += "\xE2\x9A\x99\xEF\xB8\x8F Mode: <b>" + (mode.length() ? mode : String("?")) + "</b>\n"; // ⚙️

            float percentValue = -1;
            const bool okPercent = readNumber(DESCR_Battery_Percent, percentValue);
            batteryPct = okPercent ? static_cast<int>(percentValue + 0.5f) : -1;
            modeRaw = readText(DESCR_Inverter_Operation_Mode);
            const String percent = num(DESCR_Battery_Percent, 0, "%");
            text += "\xF0\x9F\x94\x8B Battery: " + bar10(okPercent ? static_cast<int>(percentValue + 0.5f) : -1, false) +
                    " (" + percent + ")\n"; // 🔋

            const String left = timeLeftText(modeRaw, okPercent ? percentValue : -1.0f);
            if (left.length())
            {
                text += "\xE2\x8C\x9B Time left: " + left + "\n"; // ⌛
            }

            if (solarConnected)
            {
                text += "\xE2\x98\x80\xEF\xB8\x8F Solar: <b>" + num(DESCR_PV_Charging_Power, 0, " W") + "</b>  " +
                        num(DESCR_PV_Input_Voltage, 1, " V") + "\n";
            }
            float loadPercent = -1;
            const bool okLoad = readNumber(DESCR_AC_Out_Percent, loadPercent);
            text += "\xF0\x9F\x94\x8C Load: " + bar10(okLoad ? static_cast<int>(loadPercent + 0.5f) : -1, true) +
                    " (" + num(DESCR_AC_Out_Percent, 0, "%") + ")\n"; // 🔌
            text += "\xF0\x9F\x8F\xA0 Grid: " + num(DESCR_AC_In_Voltage, 1, " V") + "\n"; // 🏠
            text += "\xF0\x9F\x8C\xA1 Temp: " + num(DESCR_Inverter_Bus_Temperature, 0, " \xC2\xB0" "C") + "\n"; // 🌡

            String modeUpper = mode;
            modeUpper.toUpperCase();
            const bool onBattery = modeUpper.indexOf("BATTERY") >= 0;
            const String warning = filterAlerts(readText(DESCR_Warning_Code), solarConnected, onBattery);
            const String fault = filterAlerts(readText(DESCR_Fault_Code), solarConnected, onBattery);
            alerts = (warning.length() && fault.length()) ? warning + "; " + fault : warning + fault;
            float acIn = 0;
            gridOff = onBattery || (readNumber(DESCR_AC_In_Voltage, acIn) && acIn < 90.0f);
            readNumber(DESCR_AC_Out_Watt, loadW);
            if (warning.length() || fault.length())
            {
                text += "\xE2\x9A\xA0\xEF\xB8\x8F";
                if (warning.length()) text += " Warning: " + htmlEscape(warning);
                if (fault.length()) text += String(warning.length() ? "  " : " ") + "Fault: " + htmlEscape(fault);
                text += "\n";
            }
        }
        text += String("<i>\xF0\x9F\x93\xB6 WiFi: ") + (rssi >= -70 ? "OK" : "Low signal") + "</i>\n"; // 📶, -70 dBm boundary
        text += "<i>\xE2\x8F\xB3 Up: " + uptimeText() + "</i>"; // ⏳

        lockTake();
        summarySnapshot = text;
        snapshotMs = millis();
        lockGive();

        dashRecord(inverterConnected, gridOff, batteryPct, loadW);
        const String url = buildDashboardUrl(inverterConnected, modeRaw, alerts, solarConnected);
        lockTake();
        dashboardUrl = url;
        lockGive();
    }
};

void TelegramService::begin(std::function<bool()> networkConnected)
{
    if (_impl != nullptr)
    {
        return;
    }
    _impl = new Impl();
    _impl->lock = xSemaphoreCreateMutex();
    _impl->networkConnected = std::move(networkConnected);
    _impl->loadSettings();
    _impl->dashInit();
    configTime(0, 0, "pool.ntp.org", "time.google.com"); // UTC for the dashboard; the page shows the phone's local time
    if (xTaskCreate(Impl::taskEntry, "telegram", kTaskStack, _impl, 1, &_impl->task) != pdPASS)
    {
        LogSerial.printf("[Telegram] Failed to start task\n");
        _impl->task = nullptr;
    }
}

void TelegramService::loop(bool inverterConnected, int wifiRssi)
{
    if (_impl == nullptr)
    {
        return;
    }
    _impl->flushLogs();
    if (_impl->diagRequested.exchange(false))
    {
        const String text = _impl->buildDiag(inverterConnected);
        _impl->lockTake();
        _impl->diagText = text;
        _impl->lockGive();
        _impl->diagReady = true;
    }
    const uint32_t now = millis();
    if (_impl->lastSnapshotBuildMs && (now - _impl->lastSnapshotBuildMs) < kSnapshotIntervalMs)
    {
        return;
    }
    _impl->lastSnapshotBuildMs = now;
    _impl->buildSnapshot(inverterConnected, wifiRssi);
    _impl->checkBatteryAlerts(inverterConnected);
    _impl->checkLoadAlert(inverterConnected);
    _impl->checkInverterLink(inverterConnected);

    // Look for new firmware 2 minutes after start and then twice a day, for the summary's "new version" line.
    if (_impl->updater != nullptr && _impl->ready.load() && _impl->upgradeStage.load() == 0 && !_impl->updater->isBusy())
    {
        const bool firstDue = _impl->lastUpdateCheckMs == 0 && now > kFirstUpdateCheckMs;
        const bool periodicDue = _impl->lastUpdateCheckMs != 0 && (now - _impl->lastUpdateCheckMs) > kUpdateCheckIntervalMs;
        if (firstDue || periodicDue)
        {
            _impl->lastUpdateCheckMs = now | 1u;
            _impl->updater->requestCheck();
        }
    }
}

void TelegramService::reconfigure()
{
    if (_impl == nullptr)
    {
        return;
    }
    _impl->configChanged = true;
}

bool TelegramService::requestSummary()
{
    if (_impl == nullptr)
    {
        return false;
    }
    _impl->summaryRequested = true;
    return true;
}

void TelegramService::setUpdater(GitHubOtaUpdater *updater)
{
    if (_impl != nullptr)
    {
        _impl->updater = updater;
    }
}

bool TelegramService::pause(uint32_t timeoutMs)
{
    if (_impl == nullptr || _impl->task == nullptr)
    {
        return true;
    }
    _impl->pauseRequested = true;
    const uint32_t start = millis();
    while (!_impl->pausedAck.load() && (millis() - start) < timeoutMs)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return _impl->pausedAck.load();
}

void TelegramService::resume()
{
    if (_impl != nullptr)
    {
        _impl->pauseRequested = false;
    }
}

bool TelegramService::isReady() const
{
    return _impl != nullptr && _impl->ready.load();
}

String TelegramService::statusJson() const
{
    JsonDocument doc;
    doc["supported"] = true;
    if (_impl == nullptr)
    {
        doc["enabled"] = false;
        doc["ready"] = false;
    }
    else
    {
        _impl->lockTake();
        doc["enabled"] = _impl->enabled;
        doc["ready"] = _impl->ready.load();
        doc["botUsername"] = _impl->botUsername;
        doc["lastError"] = _impl->lastError;
        doc["chatConfigured"] = !_impl->chatIds.empty();
        doc["chatCount"] = _impl->chatIds.size();
        doc["summariesSent"] = _impl->summariesSent;
        doc["lastSummaryAgo"] = _impl->lastSummaryMs ? static_cast<long>((millis() - _impl->lastSummaryMs) / 1000) : -1;
        doc["dashboardUrl"] = _impl->dashboardUrl;
        doc["summaryPreview"] = _impl->summarySnapshot; // for checking the summary text from the local web API
        _impl->lockGive();
    }
    String json;
    serializeJson(doc, json);
    return json;
}

#else // !HAS_TELEGRAM

void TelegramService::begin(std::function<bool()>) {}
void TelegramService::loop(bool, int) {}
void TelegramService::reconfigure() {}
bool TelegramService::requestSummary() { return false; }
void TelegramService::setUpdater(GitHubOtaUpdater *) {}
bool TelegramService::pause(uint32_t) { return true; }
void TelegramService::resume() {}
bool TelegramService::isReady() const { return false; }
String TelegramService::statusJson() const { return String("{\"supported\":false}"); }

#endif
