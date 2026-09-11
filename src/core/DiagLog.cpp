#include "core/DiagLog.h"

#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <memory>
#include <new>

#include "core/LogSerial.h"

namespace
{
constexpr uint32_t kMagic = 0x44474C31;      // "DGL1"
constexpr uint32_t kPanicMagic = 0x50414E43; // "PANC"
constexpr size_t kRingSize = 6144;
constexpr size_t kLineMax = 200;
constexpr size_t kBacktraceMax = 12;
constexpr size_t kCrashLogBytes = 3000;
constexpr uint32_t kRoutineKeepSec = 300;

// Lines that repeat every few seconds. The ring keeps one of each per kRoutineKeepSec, so it spans hours, not minutes.
const char *const kRoutinePrefixes[] = {"[PI][OK]", "[Telegram] Summary sent"};
constexpr size_t kRoutineCount = sizeof(kRoutinePrefixes) / sizeof(kRoutinePrefixes[0]);

struct PanicRecord
{
    uint32_t magic;
    uint32_t pc;
    int32_t core;
    uint32_t backtraceLen;
    uint32_t backtrace[kBacktraceMax];
    uint32_t backtraceCorrupt;
    char reason[48];
};

// RTC memory keeps its contents through a crash, watchdog or software restart (not through a power cut).
struct RtcState
{
    uint32_t magic;
    uint32_t head;      // next write position in ring
    uint32_t used;      // bytes of ring in use
    uint32_t boots;     // since power-on
    uint32_t crashes;   // unexpected restarts since power-on
    uint32_t uptimeSec; // kept current by tick(), so the next boot knows how long this run lasted
    PanicRecord panic;
    char ring[kRingSize];
};

RTC_NOINIT_ATTR RtcState rtc;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
bool started = false;
char line[kLineMax];
size_t lineLen = 0;
uint32_t routineKeptAt[kRoutineCount] = {};
bool routineKept[kRoutineCount] = {};

esp_reset_reason_t resetReason = ESP_RST_UNKNOWN;
bool reportPending = false;
String reportText;
String reportLog;

std::atomic<uint32_t> wifiDropCount {0};
std::atomic<uint32_t> lastDropMs {0};
std::atomic<uint8_t> lastDropReason {0};
std::atomic<bool> wifiUp {false};

bool isUnexpected(esp_reset_reason_t reason)
{
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT;
}

// Caller holds mux.
void ringPut(const char *data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        rtc.ring[rtc.head] = data[i];
        rtc.head = (rtc.head + 1) % kRingSize;
    }
    rtc.used = (rtc.used + len > kRingSize) ? kRingSize : rtc.used + len;
}

// "h:mm:ss " uptime stamp without printf, so it is safe inside the critical section.
size_t formatStamp(char *out, uint32_t seconds)
{
    char digits[12];
    size_t n = 0;
    uint32_t hours = seconds / 3600;
    do
    {
        digits[n++] = static_cast<char>('0' + hours % 10);
        hours /= 10;
    } while (hours != 0 && n < sizeof(digits));
    size_t len = 0;
    while (n != 0)
    {
        out[len++] = digits[--n];
    }
    const uint32_t minutes = (seconds / 60) % 60;
    const uint32_t secs = seconds % 60;
    out[len++] = ':';
    out[len++] = static_cast<char>('0' + minutes / 10);
    out[len++] = static_cast<char>('0' + minutes % 10);
    out[len++] = ':';
    out[len++] = static_cast<char>('0' + secs / 10);
    out[len++] = static_cast<char>('0' + secs % 10);
    out[len++] = ' ';
    return len;
}

// Caller holds mux.
void commitLine()
{
    if (lineLen == 0)
    {
        return;
    }
    const uint32_t now = millis() / 1000;
    for (size_t k = 0; k < kRoutineCount; ++k)
    {
        const size_t prefixLen = strlen(kRoutinePrefixes[k]);
        if (lineLen >= prefixLen && memcmp(line, kRoutinePrefixes[k], prefixLen) == 0)
        {
            if (routineKept[k] && now - routineKeptAt[k] < kRoutineKeepSec)
            {
                lineLen = 0;
                return;
            }
            routineKept[k] = true;
            routineKeptAt[k] = now;
            break;
        }
    }
    char stamp[20];
    ringPut(stamp, formatStamp(stamp, now));
    ringPut(line, lineLen);
    ringPut("\n", 1);
    lineLen = 0;
}

// The newest `maxBytes` of the ring, starting at a line boundary. Allocates, so never call it while holding mux.
String copyRecent(size_t maxBytes)
{
    std::unique_ptr<char[]> buf(new (std::nothrow) char[maxBytes + 1]);
    if (!buf)
    {
        return String();
    }
    portENTER_CRITICAL(&mux);
    const size_t used = rtc.used;
    const size_t n = maxBytes < used ? maxBytes : used;
    size_t pos = (rtc.head + kRingSize - n) % kRingSize;
    for (size_t i = 0; i < n; ++i)
    {
        buf[i] = rtc.ring[pos];
        pos = (pos + 1) % kRingSize;
    }
    portEXIT_CRITICAL(&mux);

    size_t start = 0;
    if (n < used || used == kRingSize) // starts part-way through a line that has been overwritten
    {
        while (start < n && buf[start] != '\n')
        {
            ++start;
        }
        if (start < n)
        {
            ++start;
        }
    }
    for (size_t i = start; i < n; ++i)
    {
        if (buf[i] == '\0')
        {
            buf[i] = '?'; // after a brownout the memory may hold noise
        }
    }
    buf[n] = '\0';
    return String(buf.get() + start);
}

// Runs inside the panic handler: only plain stores into RTC memory.
void onPanic(arduino_panic_info_t *info, void *)
{
    PanicRecord &p = rtc.panic;
    p.pc = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->pc));
    p.core = info->core;
    p.backtraceLen = info->backtrace_len < kBacktraceMax ? info->backtrace_len : kBacktraceMax;
    for (uint32_t i = 0; i < p.backtraceLen; ++i)
    {
        p.backtrace[i] = info->backtrace[i];
    }
    p.backtraceCorrupt = info->backtrace_corrupt ? 1 : 0;
    size_t i = 0;
    if (info->reason != nullptr)
    {
        for (; info->reason[i] != '\0' && i < sizeof(p.reason) - 1; ++i)
        {
            p.reason[i] = info->reason[i];
        }
    }
    p.reason[i] = '\0';
    p.magic = kPanicMagic;
}

String buildReportText(uint32_t previousUptime)
{
    const PanicRecord &p = rtc.panic;
    const bool havePanic = p.magic == kPanicMagic;
    String text = String("Reason: ") + DiagLog::resetReasonText();
    if (havePanic)
    {
        char reason[sizeof(p.reason)];
        memcpy(reason, p.reason, sizeof(reason));
        reason[sizeof(reason) - 1] = '\0';
        if (reason[0] != '\0')
        {
            text += String(" (") + reason + ")";
        }
    }
    text += "\nIt had been running " + DiagLog::formatDuration(previousUptime) + "; unexpected restart " + String(rtc.crashes) +
            " since power-on";
    if (havePanic)
    {
        char hex[16];
        snprintf(hex, sizeof(hex), "0x%08x", static_cast<unsigned>(p.pc));
        text += String("\nPC ") + hex + " on core " + String(p.core);
        const uint32_t count = p.backtraceLen <= kBacktraceMax ? p.backtraceLen : 0;
        if (count != 0)
        {
            text += "\nBacktrace:";
            for (uint32_t i = 0; i < count; ++i)
            {
                snprintf(hex, sizeof(hex), " 0x%08x", static_cast<unsigned>(p.backtrace[i]));
                text += hex;
            }
            if (p.backtraceCorrupt)
            {
                text += " (corrupt)";
            }
        }
    }
    return text;
}
} // namespace

namespace DiagLog
{
void begin()
{
    resetReason = esp_reset_reason();
    const bool valid = rtc.magic == kMagic && rtc.head < kRingSize && rtc.used <= kRingSize && rtc.boots < 1000000;
    if (!valid || resetReason == ESP_RST_POWERON)
    {
        rtc.magic = kMagic;
        rtc.head = 0;
        rtc.used = 0;
        rtc.boots = 0;
        rtc.crashes = 0;
        rtc.uptimeSec = 0;
        rtc.panic.magic = 0;
    }
    ++rtc.boots;
    if (isUnexpected(resetReason))
    {
        ++rtc.crashes;
        reportText = buildReportText(rtc.uptimeSec);
        reportLog = copyRecent(kCrashLogBytes);
        reportPending = true;
    }
    rtc.panic.magic = 0;
    rtc.uptimeSec = 0;
    started = true;
    set_arduino_panic_handler(onPanic, nullptr);

    char marker[80];
    snprintf(marker, sizeof(marker), "=== boot %u: %s ===\n", static_cast<unsigned>(rtc.boots), resetReasonText());
    write(reinterpret_cast<const uint8_t *>(marker), strlen(marker));
}

void beginNetwork()
{
    // Runs on the Arduino event task: only counters here, tick() writes the log line from the main loop.
    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
        {
            wifiUp = true;
        }
        else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED && wifiUp.exchange(false))
        {
            lastDropReason = info.wifi_sta_disconnected.reason;
            lastDropMs = millis();
            ++wifiDropCount;
        }
    });
}

void write(const uint8_t *data, size_t len)
{
    if (!started || data == nullptr)
    {
        return;
    }
    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < len; ++i)
    {
        const uint8_t c = data[i];
        if (c == '\n')
        {
            commitLine();
        }
        else if (c != '\r' && lineLen < kLineMax)
        {
            line[lineLen++] = (c < 0x20 && c != '\t') ? '?' : static_cast<char>(c);
        }
    }
    portEXIT_CRITICAL(&mux);
}

void tick()
{
    rtc.uptimeSec = millis() / 1000;
    static uint32_t loggedDrops = 0;
    const uint32_t drops = wifiDropCount.load();
    if (drops != loggedDrops)
    {
        loggedDrops = drops;
        LogSerial.printf("[Diag] Wi-Fi connection lost (%s), %u drops since boot\n",
                         WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(lastDropReason.load())),
                         static_cast<unsigned>(drops));
    }
}

String recent(size_t maxBytes)
{
    if (!started)
    {
        return String();
    }
    return copyRecent(maxBytes < kRingSize ? maxBytes : kRingSize);
}

const char *resetReasonText()
{
    switch (resetReason)
    {
    case ESP_RST_POWERON:
        return "power on";
    case ESP_RST_EXT:
        return "reset pin";
    case ESP_RST_SW:
        return "software restart";
    case ESP_RST_PANIC:
        return "crash";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    case ESP_RST_BROWNOUT:
        return "power dip (brownout)";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "unknown";
    }
}

uint32_t bootsSincePowerOn()
{
    return rtc.boots;
}

uint32_t crashesSincePowerOn()
{
    return rtc.crashes;
}

bool crashReportPending()
{
    return reportPending;
}

const String &crashReportText()
{
    return reportText;
}

const String &crashReportLog()
{
    return reportLog;
}

void crashReportSent()
{
    reportPending = false;
    reportText = String();
    reportLog = String();
}

uint32_t wifiDrops()
{
    return wifiDropCount.load();
}

String lastWifiDrop()
{
    if (wifiDropCount.load() == 0)
    {
        return String();
    }
    const uint32_t ago = (millis() - lastDropMs.load()) / 1000;
    return String(WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(lastDropReason.load()))) + ", " + formatDuration(ago) +
           " ago";
}

String formatDuration(uint32_t seconds)
{
    char buf[24];
    const unsigned days = seconds / 86400;
    const unsigned hours = (seconds / 3600) % 24;
    const unsigned minutes = (seconds / 60) % 60;
    const unsigned secs = seconds % 60;
    if (days != 0)
    {
        snprintf(buf, sizeof(buf), "%ud %02uh", days, hours);
    }
    else if (seconds >= 3600)
    {
        snprintf(buf, sizeof(buf), "%uh %02um", hours, minutes);
    }
    else if (seconds >= 60)
    {
        snprintf(buf, sizeof(buf), "%um %02us", minutes, secs);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%us", secs);
    }
    return String(buf);
}
} // namespace DiagLog
