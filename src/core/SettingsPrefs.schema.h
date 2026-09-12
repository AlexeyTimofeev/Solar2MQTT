#pragma once

#include "pins.h"

// Telegram bot settings exist only in builds with HAS_TELEGRAM=1.
#if HAS_TELEGRAM
#define SETTINGS_ITEMS_TELEGRAM(X) \
  X(BOOL,   "telegram", "enabled", telegramEnabled, false, 0, 1) \
  X(STRING, "telegram", "token", telegramToken, "", 0, 0) \
  X(STRING, "telegram", "chatId", telegramChatId, "", 0, 0) \
  X(BOOL,   "telegram", "batteryAlerts", telegramBatteryAlerts, false, 0, 1) \
  X(BOOL,   "telegram", "gridAlerts", telegramGridAlerts, true, 0, 1) \
  X(UINT16, "telegram", "powerAlertW", telegramPowerAlertW, 5000, 500, 20000) \
  X(STRING, "telegram", "batteryAlertLevels", telegramBatteryAlertLevels, "30,25,20,15,10", 0, 0) \
  X(BOOL,   "telegram", "gridPowerAlert", telegramGridPowerAlert, true, 0, 1)
#else
#define SETTINGS_ITEMS_TELEGRAM(X)
#endif

#define SETTINGS_ITEMS(X) \
  X(STRING, "network", "deviceName", deviceName, "Solar2MQTT", 0, 0) \
  X(STRING, "network", "wifiSsid0", wifiSsid0, "", 0, 0) \
  X(STRING, "network", "wifiPassword0", wifiPassword0, "", 0, 0) \
  X(STRING, "network", "bssid0", wifiBssid0, "", 0, 0) \
  X(BOOL,   "network", "bssidLock", wifiBssidLock, false, 0, 1) \
  X(STRING, "network", "wifiSsid1", wifiSsid1, "", 0, 0) \
  X(STRING, "network", "wifiPassword1", wifiPassword1, "", 0, 0) \
  X(STRING, "network", "bssid1", wifiBssid1, "", 0, 0) \
  X(STRING, "network", "staticIP", staticIP, "", 0, 0) \
  X(STRING, "network", "staticGW", staticGW, "", 0, 0) \
  X(STRING, "network", "staticSN", staticSN, "", 0, 0) \
  X(STRING, "network", "staticDNS", staticDNS, "", 0, 0) \
  X(STRING, "network", "webUIuser", webUIuser, "", 0, 0) \
  X(STRING, "network", "webUIPassword", webUIPassword, "", 0, 0) \
  X(BOOL,   "network", "ethEnabled", ethEnabled, HAS_LAN, 0, 1) \
  X(INT32,  "device", "uartRx", inverterRxPin, PIN_INVERTER_RX_DEFAULT, -1, 48) \
  X(INT32,  "device", "uartTx", inverterTxPin, PIN_INVERTER_TX_DEFAULT, -1, 48) \
  X(INT32,  "device", "uartDir", inverterDirPin, PIN_INVERTER_DE_DEFAULT, -1, 48) \
  X(STRING, "device", "protocol", inverterProtocol, "AUTO", 0, 0) \
  X(INT32,  "device", "ds18b20Pin", ds18b20Pin, PIN_DS18B20, -1, 48) \
  X(INT32,  "device", "statusLedPin", statusLedPin, PIN_LED_STATUS, -1, 48) \
  X(UINT16, "device", "statusLedBrightness", statusLedBrightness, 128, 0, 255) \
  X(BOOL,   "device", "solarConnected", solarConnected, true, 0, 1) \
  X(UINT32, "device", "pollIntervalMs", pollIntervalMs, 100, 25, 5000) \
  X(UINT32, "device", "batteryWh", batteryCapacityWh, 0, 0, 200000) \
  X(UINT16, "device", "batteryReserve", batteryReservePct, 10, 0, 100) \
  X(UINT16, "device", "inverterIdleW", inverterIdleW, 40, 0, 1000) \
  X(UINT16, "device", "inverterEfficiency", inverterEfficiencyPct, 95, 50, 100) \
  X(UINT16, "device", "batteryFullPct", batteryFullPct, 100, 50, 100) \
  X(BOOL,   "device", "learnBattery", learnBattery, true, 0, 1) \
  SETTINGS_ITEMS_TELEGRAM(X)
