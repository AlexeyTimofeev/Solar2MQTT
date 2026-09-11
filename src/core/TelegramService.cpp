#include "core/TelegramService.h"

#if HAS_TELEGRAM

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <vector>

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

// Drops "Ok"/"0" placeholders and, when no PV is connected, every PV related entry.
String filterAlerts(const String &raw, bool solarConnected)
{
    String text = raw;
    text.trim();
    if (text.length() == 0 || text == "0" || text == "00" || text.equalsIgnoreCase("Ok"))
    {
        return String();
    }
    if (solarConnected)
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
        if (item.length() && upper.indexOf("PV") < 0)
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
    bool deleteTrigger = true;
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

    void broadcastSummary(bool silent, const String &headline)
    {
        for (const String &id : chatList())
        {
            sendSummary(id, 0, String(), silent, headline);
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
        JsonDocument markup;
        JsonArray rows = markup["inline_keyboard"].to<JsonArray>();
        JsonObject button = rows.add<JsonArray>().add<JsonObject>();
        button["text"] = "\xF0\x9F\x94\x84 Refresh";
        button["callback_data"] = "summary";
        for (const String &chat : targets)
        {
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
        deleteTrigger = _settings.get.telegramDeleteTrigger();
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
        http.addHeader("Content-Type", "application/json");
        String payload;
        serializeJson(body, payload);
        const int code = http.POST(payload);
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

    void sendSummary(const String &chat, int64_t triggerMessageId, const String &callbackId, bool silent = false, const String &headline = String())
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

        JsonDocument markup;
        JsonArray rows = markup["inline_keyboard"].to<JsonArray>();
        JsonObject button = rows.add<JsonArray>().add<JsonObject>();
        button["text"] = "\xF0\x9F\x94\x84 Refresh"; // 🔄
        button["callback_data"] = "summary";
        if (upgradeStage.load() == 0 && newVersionOffered().length()) // hidden while an upgrade is running
        {
            JsonObject upgradeButton = rows.add<JsonArray>().add<JsonObject>(); // second row, under Refresh
            upgradeButton["text"] = "\xE2\xAC\x86\xEF\xB8\x8F Upgrade"; // ⬆️
            upgradeButton["callback_data"] = "upgrade";
        }

        int64_t newId = 0;
        String body = snapshotWithFooter();
        if (headline.length())
        {
            body = headline + "\n" + body;
        }
        if (!sendText(chat, body, &markup, &newId, silent))
        {
            answerCallback(callbackId, "Failed to send summary");
            return;
        }
        answerCallback(callbackId, nullptr);
        state.lastSummaryMs = now;
        lastSummaryMs = now;
        ++summariesSent;
        taskLog("[Telegram] Summary sent (msg " + String(static_cast<long long>(newId)) + "); task stack free " +
                String(static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr))) + " B, heap " +
                String(static_cast<unsigned>(ESP.getFreeHeap())) + " B (largest " + String(static_cast<unsigned>(ESP.getMaxAllocHeap())) + " B)");

        const int64_t previous = state.lastMsgId;
        state.lastMsgId = newId;
        if (previous != 0 && previous != newId)
        {
            deleteMessage(chat, previous);
        }
        bool removeTrigger;
        lockTake();
        removeTrigger = deleteTrigger;
        lockGive();
        if (removeTrigger && triggerMessageId != 0)
        {
            deleteMessage(chat, triggerMessageId);
        }
        persistState();
    }

    void sendWelcome(const String &chat)
    {
        JsonDocument markup;
        JsonArray rows = markup["keyboard"].to<JsonArray>();
        JsonObject button = rows.add<JsonArray>().add<JsonObject>();
        button["text"] = "Refresh";
        markup["resize_keyboard"] = true;
        markup["is_persistent"] = true;
        String name;
        lockTake();
        name = botUsername;
        lockGive();
        sendText(chat,
                 "<b>Solar2MQTT</b> connected.\nPress <b>Refresh</b> below or send /summary to get the latest inverter status. "
                 "Send /restart to reboot the board. "
                 "Each new summary replaces the previous one.",
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
        sendSummary(upgradeChat, 0, String(), true);
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
                sendSummary(chat, 0, callbackId);
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
            bool removeTrigger;
            lockTake();
            removeTrigger = deleteTrigger;
            lockGive();
            if (removeTrigger)
            {
                deleteMessage(chat, messageId);
            }
            return;
        }
        if (command.startsWith("/upgrade"))
        {
            taskLog("[Telegram] Upgrade requested from chat " + chat);
            bool removeTrigger;
            lockTake();
            removeTrigger = deleteTrigger;
            lockGive();
            if (removeTrigger && messageId != 0)
            {
                deleteMessage(chat, messageId);
            }
            startUpgrade(chat); // silent like the button; progressUpgrade() re-posts the summary if nothing is installed
            return;
        }
        if (command.startsWith("/restart") || command.equalsIgnoreCase("restart"))
        {
            taskLog("[Telegram] Restart requested from chat " + chat);
            sendText(chat, "\xF0\x9F\x94\x84 <b>Restarting</b>\nThe board will be back in about 15 seconds.", nullptr, nullptr); // 🔄
            bool removeTrigger;
            lockTake();
            removeTrigger = deleteTrigger;
            lockGive();
            if (removeTrigger && messageId != 0)
            {
                deleteMessage(chat, messageId);
            }
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
            if (progressUpgrade())
            {
                continue; // hand the connection to the updater now instead of starting a long poll
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
                    broadcastSummary(true, String());
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
                if (autoOn && !chatList().empty())
                {
                    const uint32_t since = millis() - lastAutoSummaryMs;
                    if (lastAutoSummaryMs == 0 || since >= kAutoSummaryIntervalMs)
                    {
                        lastAutoSummaryMs = millis();
                        broadcastSummary(true, String());
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

    // Main thread: build the summary text from live data.
    void buildSnapshot(bool inverterConnected, int rssi)
    {
        const bool solarConnected = _settings.get.solarConnected();
        String text;
        text.reserve(512);
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
            const String percent = num(DESCR_Battery_Percent, 0, "%");
            text += "\xF0\x9F\x94\x8B Battery: " + bar10(okPercent ? static_cast<int>(percentValue + 0.5f) : -1, false) +
                    " (" + percent + ")\n"; // 🔋

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

            const String warning = filterAlerts(readText(DESCR_Warning_Code), solarConnected);
            const String fault = filterAlerts(readText(DESCR_Fault_Code), solarConnected);
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
