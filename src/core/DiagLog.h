#pragma once

#include <Arduino.h>

// Remote troubleshooting: a log ring buffer in RTC memory that survives a crash, watchdog or software restart, the
// reason for the last restart (with the crash address and backtrace from the panic handler), and Wi-Fi drop counts.
namespace DiagLog
{
void begin();                                // first thing in setup()
void beginNetwork();                         // after the Wi-Fi stack is set up
void write(const uint8_t *data, size_t len); // everything printed through LogSerial
void tick();                                 // every main loop pass

String recent(size_t maxBytes);              // newest log lines, oldest first, each with an uptime stamp
const char *resetReasonText();
uint32_t bootsSincePowerOn();
uint32_t crashesSincePowerOn();

bool crashReportPending();                   // this boot followed a crash, watchdog or brownout
const String &crashReportText();             // plain text: reason, run time, PC and backtrace
const String &crashReportLog();              // the log lines from before the restart
void crashReportSent();

uint32_t wifiDrops();
String lastWifiDrop();                       // "BEACON_TIMEOUT, 1h 10m ago", empty when none
String formatDuration(uint32_t seconds);     // "3d 04h", "2h 05m", "12m 03s", "45s"
} // namespace DiagLog
