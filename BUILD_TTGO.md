# Solar2MQTT V2.0.4 — TTGO T-Display variant

Build variant `ttgo_tdisplay_telegram` for the LilyGO / TTGO T-Display (classic ESP32, 1.14" ST7789 135x240).
Everything else is the unmodified Solar2MQTT V2.0.4 source (softwarecrash/Solar2MQTT).

## What was changed

| File | Change |
|---|---|
| `platformio.ini` | New `[env:ttgo_tdisplay_telegram]` (board `lilygo-t-display`, `HAS_TFT=1`, own pin defaults, LovyanGFX dependency) |
| `src/pins.h` | `HAS_TFT` defaults to 0 for all other variants |
| `src/core/DisplayService.h/.cpp` | New: TFT pages and button handling (compiled to a stub when `HAS_TFT=0`) |
| `src/main.cpp` | Creates the display service, `begin()` after state init, `loop()` next to the status LED |

## Screen pages (left button = previous, right button = next)

1. **BATTERY** (default at boot) — state of charge in large digits, colour bar, battery voltage and current
2. **LOAD** — AC output watts, load percent, output voltage/frequency, inverter mode
3. **SOLAR** — PV charging watts, PV voltage, PV input watts
4. **STATUS** — inverter mode (Line / Battery / ...), grid voltage/frequency, heatsink temperature, battery voltage, PV watts

Header on every page: page title, page dots, link letters W (WiFi) and I (inverter) in green/red.
Footer right: device IP address, or "setup: Solar2MQTT AP" while in setup mode.

## Pins (defaults; stored in the device settings, changeable with `POST /api/settings/device`)

| Function | GPIO | Note |
|---|---|---|
| Inverter TX (ESP -> MAX3232 T1IN) | 26 | right header |
| Inverter RX (ESP <- MAX3232 R1OUT) | 27 | right header |
| RS485 DE/RE (Modbus inverters only) | 25 | unused for PI30 |
| DS18B20 temperature sensor | 21 | optional |
| Status LED | none (-1) | board has no user LED |
| TFT (fixed) | 4 BL, 5 CS, 16 DC, 18 SCLK, 19 MOSI, 23 RST | do not reuse |
| Buttons (fixed) | 35 next, 0 previous | |

Build flags you may want to change: `TFT_ROTATION` (1 or 3 flips the screen), `TFT_BUTTON_NEXT`, `TFT_BUTTON_PREV`.

## Build and flash

    export PATH="$HOME/.local/bin:$PATH"
    pio run -e ttgo_tdisplay_telegram              # compile
    pio run -e ttgo_tdisplay_telegram -t upload    # flash over USB (auto-detects the port)

Outputs after a build:

* `.firmware/Solar2MQTT_ttgo_tdisplay_V2.0.4.bin` — full image, flash at offset 0x0 with esptool or a web flasher
* `.firmware/Solar2MQTT_ttgo_tdisplay_V2.0.4.bin.ota` — app image for a LAN upload:
  `curl -F "firmware=@<file>.bin.ota" http://<board>/update_firmware` (settings are kept, the board restarts)

The built-in GitHub update check cannot find a release asset for this variant, so it reports
"No matching asset for build"; update it by rebuilding and uploading the `.bin.ota` instead.

# Telegram variant (`ttgo_tdisplay_telegram`)

The T-Display build includes a Telegram bot (`HAS_TELEGRAM=1`). MQTT support has been removed (since 2.1.17): the bot
and its Dashboard replace it, and the board's own web page only configures Wi-Fi and the bot.

## Web page

* Start page: Wi-Fi, bot and inverter status, firmware version, and two buttons: **Wi-Fi settings** (the setup page,
  also served by the setup access point "Solar2MQTT-AP") and **Telegram bot** (enable, token, chat IDs, status,
  "Send summary now"). The bot page saves only those three fields, so it never overwrites the Dashboard's ⚙️ Settings.
* Everything else (alerts, battery values, the inverter's priorities and grid charging limit) is in the Dashboard's
  ⚙️ Settings in Telegram.
* No page, but kept for maintenance: `GET /api/status`, `/api/data`, `/api/settings`, `/api/telegram/status`,
  `/api/debug/report`, `/ota/status`; `POST /update_firmware` (LAN upload), `/ota/check`, `/ota/update`,
  `/api/settings/device` (pins, protocol, battery values), `/api/command`, `/api/reboot`; the Web Serial log socket
  `ws://<board>/webserialws`. Old page addresses (`/status`, `/menu`, `/settings`) open the start page.

## What the bot does

* The summary (mode, battery, time left on battery, solar, load, grid, temperature, warnings, Wi-Fi, uptime) keeps
  itself up to date. Its only button is **📊 Dashboard** (plus ⬆️ Upgrade when a newer release is out; a Refresh
  button appears only when the dashboard link is unavailable). `/summary` sends a fresh one at the bottom.
* The chat only keeps the latest summary: a new one deletes the previous message (or edits it in place, see
  "Automatic summary"), and your own command message (`/summary`, Refresh, `/diag`, ...) is always removed.
* `/start` (or `/help`) sends a short welcome and a first summary. The persistent Refresh keyboard of older firmware
  is removed: by `/start`, and once by the board itself after the update.
* Only the paired chat id is served. Before pairing, any chat that writes to the bot gets its chat id back
  so you can enter it in the web UI. Everything else is ignored.
* Transport: one long-polling HTTPS connection to `api.telegram.org`, certificate pinned to the Go Daddy Root G2
  (`src/core/TelegramRootCa.h`), running in its own FreeRTOS task so the inverter polling and the screen never wait
  for Telegram.

## Setup

1. In Telegram, talk to `@BotFather`, `/newbot`, copy the token.
2. Open the device web page, **Telegram bot**: paste the token, enable the bot, Save. Leave Chat ID empty.
3. Open your new bot in Telegram and send `/start`. It replies with your chat id.
4. Enter that chat id in the web UI, Save. Send `/start` again: you get the first summary with its Dashboard button.
5. "Send summary now" on the settings page pushes a summary to the paired chat for testing.

## Files added or changed for this variant

| File | Change |
|---|---|
| `platformio.ini` | `[env:ttgo_tdisplay_telegram]` with `-DHAS_TELEGRAM=1` |
| `src/core/TelegramService.h/.cpp` | New: bot task, Bot API client, summary text, buttons, message cleanup |
| `src/core/TelegramRootCa.h` | New: pinned root certificate |
| `src/core/SettingsPrefs.schema.h` | `telegram` settings group (enabled, token, chatId, alerts, automatic summary), only when `HAS_TELEGRAM` |
| `src/core/WebServerHandler.*` | `/telegram` page, `/api/settings/telegram`, `/api/telegram/status`, `/api/telegram/test`; only the Wi-Fi and bot pages remain |
| `src/webUI/index.html`, `telegram.html` | Start page and bot page, each with a small inline script (no shared app.js / status bar) |
| removed | `MqttHandler`, `HaDiscoveryCatalog.h`, PubSubClient, the `mqtt` settings group, and the status, menu, MQTT, device, firmware, debug and Web Serial pages |
| `src/main.cpp` | Service start and 2-second summary snapshot refresh |

Notes: the bot token is stored in NVS and appears in the settings backup. Telegram deletes messages older than
48 hours only for the bot's own messages, so a very old summary may remain if the device was offline for days.

## Solar on/off switch

While the inverter runs on battery (Mode: Battery) the summary leaves out the "Line fail" warning or fault; the mode line
already shows that the grid is out.

The Dashboard's ⚙️ Settings has "Solar panels connected" (key `device.solarConnected`, default on). Switch it off
while no PV is wired: the Telegram
summary drops the Solar line and every PV related warning or fault (for example "PV loss warning"), and the display
skips the SOLAR page. Setting key: `device.solarConnected`.

## Battery alerts

The Dashboard's ⚙️ Settings has a "Battery low at, %" field: comma separated levels (key `telegram.batteryAlertLevels`, default
30,25,20,15,10, up to 8 levels of 1-99 %); an empty field switches the alerts off (key `telegram.batteryAlerts`). The settings code
carries the levels as an optional 8th field of `s3` (`30-20-10`, `0` = none).
When the battery percentage falls through one of these levels the bot sends a new summary with sound and a
"🪫 Battery below 25 %" headline; the previous summary is removed, so there are no separate alert messages. A level re-arms once the battery has climbed 3 points above it, and the
tracking restarts whenever the inverter link drops, so reconnects never produce false alerts. Alerts are delivered
between long-poll cycles, so expect up to about 20 seconds of delay.

## Grid alerts

The Dashboard's ⚙️ Settings has "Grid off / back" (key `telegram.gridAlerts`, default
on). When the inverter runs on battery (or reports no AC input) for 10 seconds, the bot sends a new summary with sound and
a "🔴 Grid off" headline; when the grid has been back for 10 seconds, one with "🟢 Grid back after 2h 13m". Shorter
dips are ignored, nothing is announced while the inverter itself is unreachable, and the state found at boot is not
announced. During an outage the summary's grid line reads "off for 2h 13m". As with every alert, the previous summary
is removed, so the chat still holds only the summary. Test builds with `-DGRID_ALERT_TEST` accept `gridoff`, `gridon`
and `gridreal` in the web serial console; `gridhigh` fakes a grid power of 5.5 kW.

## Power alert

The Dashboard's ⚙️ Settings has a "Power alert above" dropdown (`telegram.powerAlertW`, default 5000 W; 1-6 kW on the
page, 0.5-20 kW in the firmware). It has no on/off switch: the page always keeps `telegram.gridPowerAlert` on, 6 kW keeps
it practically quiet, and an alert switched off earlier shows as 6 kW. While the grid is on it watches the grid power, estimated as on
the dashboard (appliances + battery charging from the grid / efficiency + the inverter's own consumption); during an
outage it watches the house load the inverter carries. Once the power has stayed above the threshold for 10 seconds
the bot sends one new summary with sound and a "⚡ Grid power 5.3 kW, above 5 kW" (or "⚡ Load 5.3 kW on battery, above
5 kW") headline; it re-arms after the power has stayed below the threshold for 30 seconds. Since 2.1.17 this replaces
the separate high load alert.

## Alert log

Every loud alert is also written to a 25-entry log kept in FLASH (NVS namespace `alerts`), so it survives a power cut
as well as a firmware update - unlike the 24 h history, which churns every 15 minutes and stays in RTC. Alerts are rare
(a few a day), so one flash write each costs nothing.

Everything funnels through `requestLoud(headline, type, value)`, which calls `noteAlert()`: `g` grid off, `G` grid back,
`l` load above the threshold on battery, `p` grid power above it, `b` battery below a level, `o` inverter offline,
`r` unexpected restart (logged once per boot in `sendCrashReport`, not once per delivery attempt), `W` Wi-Fi signal
low, and `i` / `I` for an inverter batch that was applied or had something refused. `noteAlert` copies the log under the lock and writes flash
outside it, because the restart alert is raised on the bot task and everything else on the main thread.

* **Summary**: the last five, newest first, at the very end - after Updated and Version - as a blank line, then
  `⚠️ Last alerts` (no colon), then one `14 Sep 11:15 🔵 Grid on` line each: the dated column, one space, the
  severity icon, then the text - no bullets. Three levels, like a log: 🔴 error (unexpected restart, inverter
  offline), 🟡 warning (load or grid draw above the threshold, battery low, Wi-Fi low, inverter change refused),
  🔵 info (grid on/off, settings saved). The Dashboard shows the same three (`alertName` in the page, `alertIcon()`
  in the firmware), so one alert reads the same on both surfaces. Built in `footerFor(text, age, lead)`,
  which takes no lock of its own; `snapshotWithFooter()` reads the snapshot under the lock and releases it before
  calling it. **Never call `footerFor()` or `alertLogText()` while holding the lock**: doing exactly that in the
  `/api/telegram/status` handler deadlocked the endpoint outright, and the task watchdog then reset the board twice -
  which the alert log itself faithfully recorded as two unexpected restarts.
* **Timezone** (`device.tzOffset`, whole hours -12..+14, default +3): alerts are dated with the wall clock instead of
  an age. `alertWhen()` shifts the stored UTC time by the offset and formats "14 Sep 11:15"; anything recorded before
  SNTP synced has `at == 0` and reads "time unknown" rather than a 1970 date. The Dashboard uses the SAME offset (link
  key `tz`) and derives each alert's absolute time from the link's own `t` minus the alert's age, so the two surfaces
  can never disagree. The Board tab has a UTC-12..UTC+14 dropdown.
  Encoding traps, all deliberate: the offset rides in the save code as **field 9 shifted by +12** (0..26) because `-`
  already separates the battery alert levels; `applySettingsCode` gained an explicit `count == 9` branch (7 / 8 / 10
  keep their old meanings, 10 being the legacy 2.1.17 tail); and the levels test widened to `count == 8 || count == 9`
  or a 9-field code would silently drop the levels. The page's regex captures the field ahead of its trailing
  catch-all, so older cached pages still parse a 9-field code and keep saving 8.
* Only a genuine fault raises the restart alert: `DiagLog::isUnexpected()` covers panic, the watchdogs and brownout,
  so an ordinary OTA restart (`ESP_RST_SW`) never adds one.
* **Wi-Fi low** (`W`, built in, no setting): `checkWifiSignal()` mirrors the grid debounce. The signal must stay below
  `kWifiLowDbm` (-70, the same boundary the summary calls "Low signal") for `kWifiConfirmMs` (60 s - far longer than
  the grid's 10 s, because signal strength fluctuates constantly), and it re-arms only once it recovers past
  `kWifiOkDbm` (-65). That 5 dB of hysteresis is the point: without it a link sitting at the boundary would alert over
  and over. There is deliberately no "recovered" alert. The value carried is the positive dBm, so `alertText()` prints
  it back as "Wi-Fi low, -72 dBm".
* **Dashboard**: a foldable "Alerts" panel between the 24 h chart and Settings, listing all 25 newest-first, hidden
  when there are none. Link key `al`: six characters per alert - type, value in base36 (2), minutes ago in base36 (3),
  oldest first, so 25 alerts cost 150 characters. The page must use `Math.floor` for the age exactly as the firmware
  uses integer division, or the same alert reads differently in the two places.
* Like the history, the log shrinks when Telegram rejects the link (`dashAlertsAllowed` halves alongside
  `dashSlotsAllowed`).

## Learned battery model

With "Learn from outages" (key `device.learnBattery`, default on) the board learns the battery model itself while the
inverter runs on battery without solar input, one sample per 2-second snapshot:

* Battery power (volts × discharge amps) against the house load is a straight line: its slope is 1 / efficiency and
  its offset the inverter's own use. It is used after 30 minutes on battery with the load varying by at least 100 W
  (standard deviation), and only if the result is plausible (75-100 %, 0-200 W).
* Capacity = energy drawn ÷ % of charge used, summed over outages in which the charge fell by 8 points or more.
* The sums are kept in flash (NVS namespace `learn`), saved when an outage ends and every 30 minutes during one.
  Once known, the learned values replace the stored ones in every estimate (time left, grid power, the dashboard);
  the ⚙️ panel shows what has been learned, and "Forget" clears it (flag bit 128 of the settings code, once).
  `/diag` has a "Battery model:" line.

## Inverter settings from the Dashboard

The ⚙️ panel is grouped: 🔔 Alerts and 🔋 Battery & estimates (the board's own settings, sent with **Save**) and
⚡ Inverter configuration (stored in the inverter, sent with **Apply to inverter**). Every setting below was tested on
the PowMr VMII-6000 with a no-op write of its current value:

| Setting | Command | Values |
|---|---|---|
| Output priority | `POP0n` | 0 Utility first, 1 Solar first, 2 Battery first (SBU) |
| Charger priority | `PCP0n` | 1 Solar first, 2 Solar + utility, 3 Only solar (only offered with solar panels connected, since it stops charging from the grid). **0 Utility first is refused by this VMII-6000** (`PCP00` NAK three times, read-back unchanged, 2026-09-13); the panel should not offer it |
| Grid charging, max | `MUCHGCn` | **not settable over the serial link on this VMII-6000**: every real change answers NAK (`MUCHGC50` and `MUCHGC10` from the panel, `MUCHGC0050` directly), only the value already set is ACKed (`MUCHGC60`; `MUCHGC060` / `MUCHGC0060` NAK), while the inverter's own menu changes it freely. The panel shows it read-only (≈ kW at 54 V / 95 % and the amps). The PowMr HVM Modbus protocol of the WIFI-VM dongle (slave 5, 2400 baud, holding registers 4501+, utility charge current 5024) gets no answer on this port either: three read-only probes with test build 2.1.19-mb1 (branch `modbus-probe`, custom command `MB:<hex>`) came back empty and left the PI30 link untouched |
| Total charging, max | `MNCHGCnnn` | no row: with the BMS battery the inverter reports the BMS's current limit here (10 A near full, 90 A otherwise) and refused the one real change tried (`MNCHGC050`); the no-op `MNCHGC090` was ACK (three digits, `MNCHGC0090` NAK) |
| Grid input range | `PGR0n` | 0 Appliance (wide window), 1 UPS (fast switch-over) |
| Buzzer, overload bypass, restart after overload, restart after overheating | `PEa`/`PDa`, `b`, `u`, `v` | on / off (backlight `x`, grid-loss alarm `y` and power saving `j` never answer on this model) |
| Battery % points (lithium with BMS): back to grid, back to battery, cut-off | `PBCC`, `PBDC`, `PSDC` + `nnn` (3 digits), read with `QDOP` (fields 9-11) | 5-95, 10-100, 0-90 %, any whole % (2026-09-13 full test: cut-off 0/5/10/20/30/40/50, back to grid 5/10/50/95, back to battery 10/50/80/100, each stored as sent); cut-off <= back to grid < back to battery; back to grid / back to battery are shown only in Solar first or Battery first mode (the only modes that use them); `PSDC` answers only after a ~20 s pause |

The panel uses dropdowns that list only accepted values (the % points at whole percent, the voltages in 0.1 V steps; also the power alert and cut-off).

**Full real-change test, 86 commands, 2026-09-13 (every setting restored, QPIRI/QFLAG/QDOP verified identical to the start):** every exposed setting is genuinely settable, confirmed by reading QPIRI/QDOP back — NOT by the answer byte. Float `PBFT` accepted the whole 53.6-56.0 V range; bulk `PCVV` the whole 55.2-57.6 V range (57.6 = BMS charge limit); cut-off `PSDC` 0/5/10/20/30/40/50; `PBCC`/`PBDC` arbitrary whole %; all four flags on and off; input range both ways; output priority 0/1/2 (POP02 = SBU really moves the house to Battery mode, POP00 back to Line). The one genuine rejection among priorities is charger priority 0 (Utility first), see the table.

**KEY: this inverter's answer byte is unreliable.** About 8 of the 86 setters answered `NOA` (empty) or even `NAK` while the setting still changed correctly (proven by read-back): `PBFT54.9`/`55.2` NOA, `PCVV56.2` NAK, `PCVV56.4`/`57.4` NOA, `PGR01` NAK, `PSDC010` NOA (plus one `QMN` CRC error). So `ACK`/`NAK`/`NOA` cannot be trusted on this unit, and the current "⚠️ Inverter: refused / (no answer)" headline (built from the answer byte) will sometimes be wrong. **Fixed on branch `verify-readback`: the headline is built from a read-back, not from the answer byte.** Each finished command is kept with its intended value; `kInverterVerifyDelayMs` (8 s) after the last one — and after QDOP when a % point changed — `verifyCommand()` compares the inverter's own QPIRI / QFLAG / QDOP values against what was asked and `finishInverterBatch()` writes "setting ✓" or "setting not applied" (commands that cannot be read back keep the old ACK/NAK wording).** This also correctly catches the true refusals (grid charging, charger-priority Utility-first), where the read-back does not move. The allowed currents are asked once a
minute after start. Apply sends `/start i1_<key><value>_...` with only the
changed settings (Telegram allows 64 characters, too few next to the board's settings): o, c, g, z buzzer (u and t are still accepted, the page no longer sends them),
y bypass, w restart after overload, q restart after overheating, and the battery % points b back to grid, e back to
battery, s cut-off. The main loop checks the whole code against the inverter's current values (ranges, grid <= total,
cut-off % <= back to grid % < back to battery %) and sends nothing if any part is wrong ("⚠️ Inverter: nothing changed
(...)"). % changes are ordered so the chain stays valid on the way (lowered values from the bottom up, raised ones from
the top down). Commands go one at a time, 8 s apart (20 s before `PSDC`; flag commands that follow another command
closely go unanswered), with one retry when there is no answer; setting commands make the driver re-read QPIRI and
QFLAG, so the Dashboard shows the new values. The summary then shows a silent "✅ Inverter: total charging 100 A ✓,
restart after overload on ✓" headline until the next automatic edit. The link carries `io ic iu il it itl ig ix sg sd sc
bms`. Firmware 2.1.17 (no `it` key) still gets its priorities and grid charging as a tail on the Save code.

Only lithium batteries with BMS communication are supported. The switch-over and cut-off voltages (`PBCV`, `PBDV`,
`PSDV`) are not offered: with a BMS the inverter uses the % points. The charging voltages are: contrary to the manuals,
this VMII-6000 in LIb mode floats the battery at its own float voltage (`PBFT`). With float 54.0 V the battery stayed at
88 % (0 A, "Float"); after `PBFT55.2` it took current again and reached 94 % within 10 minutes (2026-09-13). The panel
offers Float (53.6-56.0 V) and Bulk (55.2 V up to the BMS charge voltage limit from QBMS, 57.6 V here) in 0.1 V steps (float 56.0 V took it to 100 %),
float <= bulk; Apply keys `k` (bulk, `PCVV`) and `f` (float, `PBFT`), volts x10; link keys `vb` / `vf`.

The board asks `QDOP` (battery % points) and `QBMS` (what the BMS reports: connected, SOC, force-charge / stop-discharge
/ stop-charge flags, C.V. and float voltage, cut-off voltage, max charge and discharge current) a minute after start,
every 6 hours and after a % change; queries no longer make the driver re-read QPIRI (only setting commands do). With a
cut-off % known, the discharge estimate stops there (the Battery & estimates cut-off field is then hidden), and the
Dashboard's details show a "Battery BMS" line. Protocol source: Voltronic "Axpert Remote Panel Protocol (VMIII&KING&MKSIII)
20220616" (github.com/ardupic/voltronic-inverter-communication-protocols); on BMS loss the inverter stops (code 61), it
does not fall back to the voltage setpoints.

Found but not offered: the inverter has hour-by-hour output and charger priority tables (`QOPPT`, `QCHPT`) and LED
settings (`QLED`), but the commands to change them are unknown.

## Memory notes

The Telegram TLS session keeps roughly 55 KB of heap while connected, and every web UI request copies the state
document, so the display draws straight to the panel with padded text instead of keeping a 32 KB off-screen buffer.
Each summary logs the remaining task stack, free heap and the largest free block on the serial console and Web Serial
(`[Telegram] Summary sent ...`). If the largest free block drops below about 10 KB in normal use, reduce load on the
web page. Since 2.1.17 there is no MQTT client and no status websocket, which frees roughly 4 KB of heap.

## Automatic summary

Always on since 2.1.17 (the Dashboard button's data comes with it; the `telegram.autoSummary` setting is gone). Every 15 seconds the bot
edits the last summary in place (no new message, no notification); the fallback Refresh button, the first summary after a
restart and the summary after an upgrade attempt do the same. If there is no summary yet or it can no longer be edited
(for example the user deleted it), a new silent one is sent instead. A typed `/summary` or the keyboard Refresh still
sends a fresh summary at the bottom and deletes the old one, and alerts (power, battery, grid, inverter offline) arrive
as a new summary with sound. Any manual summary restarts the timer. The long poll is shortened as the next automatic summary comes
due, so the interval stays close to 15 seconds.

## Summary format

One `<code>` block, so the values line up in a column: `<emoji> <label padded to 11 by padLabel()><value>`, no colons.
`<code>` and not `<pre>`: Telegram offers no plain "monospace" formatting - only the two code entities - and current
clients dress a `<pre>` block as a code panel with a bar down the left and a Copy button on top, while `<code>` gives
the same fixed-width columns with none of that chrome.
Lines, in order: ⚙️ Mode, 🏠 Grid, 🔋 Battery (the percentage alone), ☀️ Solar (hidden when solar is switched off),
🔌 Load (the percentage alone), 🌡 Temp, ⚠️ Warning / Fault (full width, not a label/value pair), 📶 WiFi (OK at
-70 dBm or better, otherwise Low signal), ⏳ Up (uptime), 🕒 Updated, 💾 Version. Grid sits directly under Mode so the
block matches the lead's order. The moon glyphs are gone - the number already said what they said - and `bar10()` went
with them. The device-name header was removed on request.

**Lead line**: one plain line above the block - `⚙️Line 🏠223.7V 🔋95% 🔌22% 🌡51° 📶OK ⏳8m 🕒1s 💾2.1.23` - built in
`buildSnapshot()` as `summaryLead` and stored under the same lock as the snapshot itself. It exists for the Telegram
*chat list*, which previews a message with formatting stripped and truncates it around 40 characters, so the fields are
ordered by what is worth seeing there and the block's own icons stand in as labels to keep it short. It is deliberately
redundant with the block below, and deliberately *outside* the `<code>` wrap so it cannot disturb the columns, with a
blank line between the two. Both
`footerFor()` call sites read it under the lock and pass it in: `snapshotWithFooter()` and the `/api/telegram/status`
handler - which is also where 🕒 Updated and 💾 Version join it, because the age is only known when the snapshot is
read, not when it is composed.

**Nothing inside the block may carry `<b>` or `<i>`**: Telegram renders a code block verbatim and rejects a message whose
entities are nested inside it. So the wrap happens in exactly one place - the `return` of `footerFor()` - and the alert
headlines, which all carry `<b>`, are prepended *above* it by the single composition site (`body = headline + "\n" +
body`). The padding is ASCII-only, so a label is always 11 characters. It was 11 to put the values on the same column
as the alert text; the alert rows have since taken a severity icon and a single space, so the two blocks no longer
share a column - 11 stayed because it reads well on its own. The leading emoji are not ASCII (⚙️ is
U+2699 plus a variation selector, 🔋 is one code point) and clients render them at slightly different widths, so a
column can still look a hair uneven on some phones - a known trade-off of keeping the icons.

`gridText()` broke this rule from the moment the block was introduced: it returned `<b>off</b> for 5m`, which appears
only while the grid is down - so the summary would have failed during an outage and at no other time. No release ever
carried it (2.1.23 and earlier have no code block, where that bold `off` was correct); it lived in the branch builds
ms1-ms3 and was fixed in ms4. `footerFor()` now strips `<b>` and `<i>` from the body before wrapping, as a net. The
lesson is how it was missed: grepping `text += ...<b>` finds only markup written inline, and this arrived from a
helper's *return value*. Audit what flows into the block by data flow, not by searching for a pattern.

## High-load summary

Removed in 2.1.17 (the `telegram.loadAlert` setting is gone): the power alert covers it, with the house load above the
threshold while the inverter runs on battery. Battery and Load show the percentage alone - no glyph, no colour cue. The Grid line shows voltage only, and WiFi, uptime and Updated are separate lines.

## Multiple chats

`telegram.chatId` accepts a comma-separated list of chat ids (private chats or groups). Any listed chat can request a
summary with /summary, the Refresh buttons or a plain "Summary"/"Refresh" message, and its previous summary is deleted
in that chat only. Automatic summaries and every alert (grid, power, battery) are sent to every listed chat. The
last summary message id is stored per chat in NVS (`tg/lastMsgs` as JSON), so cleanup keeps working across reboots.

## Inverter offline notice

Always on: when the inverter link has been down for 15 seconds after having worked, the bot sends one summary with
sound and a "🔴 Inverter offline" headline to every chat, at most once per five minutes. Nothing is sent at boot before
the inverter was ever seen.

# CrowPanel 3.5" variant (`crowpanel35_telegram`, parked)

Target: Elecrow CrowPanel ESP32 3.5" HMI (DIS05035H), classic ESP32, ILI9488 320x480 over SPI, XPT2046 resistive
touch, case included. The display code is now resolution independent: layout and fonts scale from the T-Display
reference (240x135) to the real panel size, and a tap on the left or right half of the touch panel replaces the two
T-Display buttons (`TFT_TOUCH=1`, buttons disabled with `TFT_BUTTON_NEXT/PREV=-1`).

Pins (verified from Elecrow schematics, see docs-crowpanel35.md): SCLK 14, MOSI 13, DC 2, CS 15, RST tied to EN,
backlight 27 (active high), touch IRQ 36; on V2.2 boards MISO 33 and touch CS 12, on V1.x/V2.0/V2.1 boards MISO 12 and
touch CS 33 (build with `-DCROWPANEL35_HW_V20`). LovyanGFX rotation 1 gives landscape with USB-C on the right. Inverter UART defaults: RX 21, TX 22 (the I2C header, free because
the touch controller is on SPI); DS18B20 on 32 (GPIO header). UART0 (1/3) stays on USB for flashing and logs.

Bring-up checklist on real hardware (from the code review): confirm landscape orientation (`TFT_ROTATION` 1 or 3),
colour inversion and RGB order, touch axis mapping (adjust `offset_rotation` in the touch config if left/right taps
land top/bottom), and that the PENIRQ pull-up exists so `getTouch` works. Touch calibration defaults come from Elecrow's ESPHome config; recalibrate per unit if taps land off-centre.

# CrowPanel Advance 3.5" variant (`crowpanel_adv35_telegram`, parked)

Target: Elecrow CrowPanel Advance 3.5-HMI (ESP32-S3-WROOM-1-N16R8, 480x320 IPS, ILI9488 over SPI, GT911 capacitive
touch, acrylic case option, ~$27.70 with case direct from Elecrow). Pins come from Elecrow's own LovyanGFX driver and
the Meshtastic variant; details and sources are in docs-alt-boards.md.

* Inverter link: the J15 "UART1-OUT" Grove connector, pins RX IO18 / TX IO17 / 3V3 / GND, wired to the MAX3232.
* Flashing and logs: the CH340K USB-C port (UART0). `ARDUINO_USB_CDC_ON_BOOT=0` keeps logs there.
* Touch: tap left half = previous page, right half = next page. No physical buttons.
* DS18B20: not assigned by default. IO10 or IO9 on the wireless header can be used, but only with IO45 driven LOW
  (disables the microphone) and no wireless module fitted; set `-DPIN_DS18B20=10` and add that pin handling if needed.
* Keep the wireless slot empty: GPIO2 is shared between the LCD reset and the slot's reset/CSN line.
* Partition scheme `default_16MB.csv` (two 6.25 MB app slots); the web-UI OTA works the same way.

The Sunton ESP32-8048S043C (4.3", 800x480 RGB) is documented in docs-alt-boards.md with a verified LovyanGFX config;
it is not built here because its RGB panel needs a PSRAM framebuffer and extra tuning against Wi-Fi flicker.

## /restart command

Sending `/restart` (or the word "restart") from a paired chat reboots the board: the bot confirms with "🔄 Restarting",
removes the command message, saves its state, and triggers the firmware's
normal restart path 1.5 s later. It is listed in Telegram's command menu next to /summary and /start.

# Sunton ESP32-3248S035 3.5" variants (`cyd35r_telegram`, `cyd35c_telegram`, parked)

Target: the generic 3.5" "ESP32 LCD TFT Module" from AliExpress (Sunton/JC ESP32-3248S035, classic ESP32, 4 MB flash, no
PSRAM, ST7796 320x480 over SPI). Use `cyd35r_telegram` for the resistive "R" board (XPT2046, comes with a stylus) and
`cyd35c_telegram` for the capacitive "C" board (GT911, glossy glass). Hardware notes and sources: docs-cyd35.md.

* Inverter link on the P3 header: RX GPIO35 (input only), TX GPIO22, GND; 3V3 for the MAX3232 from CN1.
* DS18B20 on GPIO21 (4.7 k pull-up; the C board already has a 10 k).
* Flash/logs over the board's micro-USB or USB-C (CH340C, 460800 baud). Do not use the P1 UART header while USB is plugged in.
* Rotation 1 = landscape with USB on the right (3 = left). Touch calibration values are community defaults; if left/right
  taps land the wrong way on the R board, change the touch `offset_rotation` in DisplayService.cpp.
* Older boards carry a second flash chip (U4) in parallel with the module flash that can disturb uploads; the USB-C
  revision has it removed. No case in the box; printable cases: Printables 739905, Thingiverse 6807372, MakerWorld 1151020.

## Inverter link health

`PI_Serial` judges the link by the last valid reply (`kPiLinkHoldMs`, 20 s) instead of requiring a complete dynamic
cycle every 5 s; at 2400 baud one cycle takes 3-5 s, so the original rule turned a single slow reply into a visible
"Inverter not connected". A NAK counts as a valid reply, because it proves the inverter heard the request.
Query commands (`Q...`, `^P...`) are retried once after a timeout, preceded by a 250 ms drain so a late frame cannot be
mistaken for the retry's answer; setters are never repeated. `/api/data` exposes `PI_Ok`, `PI_NoAnswer`, `PI_CrcError`,
`PI_RetrySaved`, `PI_SilenceMs` and `PI_LongestSilenceMs` for remote diagnosis.

Set `ds18b20Pin` to -1 when no temperature probe is attached: discovery otherwise blocks the main loop for about
half a second every ten seconds.

## Battery reading guard and alert confirmation

Some PI30 firmwares occasionally report battery capacity `000` in an otherwise valid QPIGS frame (checksum, voltage and
load all fine). `PI_Serial::guardBatteryPercent` keeps the previous value when the percentage falls more than 30 points
between two polls while the battery voltage changes by less than 1 V; if the low value persists for a minute it is
accepted. Each rejection is logged with the raw frame (`[PI][WARN] battery ... is implausible`) and counted in
`/api/data` as `PI_BattRejected`. The guard protects the display, summaries and MQTT as well as the alerts.

Battery alerts additionally require a level to stay crossed for 30 seconds, and a drop through several levels at once
sends a single message for the lowest one (previously one message per level, and the fifth was dropped by the queue).

# Maintained builds (2026-09-11)

Only `ttgo_tdisplay_telegram` is maintained (running on the board at the inverter). The plain `ttgo_tdisplay` build
without the bot is parked as well, since the bot can simply be switched off in the settings. The CrowPanel and Sunton builds are parked in `parked-boards.ini`, which PlatformIO does not read; see
that file's header to revive one when the board is available. Their board classes remain in `DisplayService.cpp`,
compiled out, and their hardware research stays in the `docs-*.md` files.

## Firmware updates, /upgrade and rollback

CI publishes a release of this fork for every `v*` tag, and the board's updater reads `AlexeyTimofeev/Solar2MQTT`.

* Without Telegram: `POST /ota/check`, then `POST /ota/update` on the board (there is no Firmware page any more).
* Summaries end with "New version X available", and get an Upgrade button under Refresh, once the board's own check (2 minutes after start,
  then every 12 hours) has found a newer release.
* Telegram: the Upgrade button (or `/upgrade`) installs the newer release without any chat messages; the button only
  shows a short pop-up, and no automatic summaries are posted while it installs. After the restart a fresh summary
  replaces the old one, so the version line and the button change together. If nothing was installed (already up to date, or the check or download failed) the summary is simply
  re-posted, and after a failure it keeps the Upgrade button for a retry. The outcome is in the board log.
* After any restart, the first summary (automatic summaries, or the one after an upgrade) goes out 45 seconds after
  boot, once the inverter has reported, rather than right away with blank values. A Refresh press is answered at once.
* A classic ESP32 cannot hold two TLS sessions at once, so the updater pauses the bot's connection while it talks to
  GitHub; alerts raised meanwhile are sent when the bot is back.
* The download's TLS session leaves little free memory, and on this toolchain a failed `new` aborts the board (2.1.8
  and 2.1.9 crashed this way in `AsyncWebSocket::textAll` from the web status refresh). So while the updater works the
  once-a-second web status refresh pauses, the status is only pushed when a web client is connected, and the web serial
  console skips lines while the largest free block is under 8 KB. Avoid polling the web API during a download.
* At boot the board waits up to 10 s more for Wi-Fi before starting the setup AP; after a crash the router can take
  about 15 s to accept it again.
* A newly installed image runs on probation: until the bot has connected, or for three minutes, a restart makes the
  bootloader roll back to the previous firmware. Power-cycling the board in that window therefore also rolls back.
* The board shares GitHub's limit of 60 unauthenticated API requests per hour with every other device on the same
  internet connection.

Summaries end with `💾 Version: <version>`, followed by the new-version line when one is available.

## Remote troubleshooting

* `/diag` replies with the firmware and partition, uptime, the reason for the last restart, Wi-Fi (network, signal,
  channel, drops and the last disconnect reason), memory, the inverter link counters, and the Telegram, update and
  MQTT status.
* `/log` sends the recent board log as a text file. The log is a 6 KB ring buffer in RTC memory with an uptime stamp
  per line; the routine inverter and summary lines are kept once per 5 minutes, so the buffer spans hours. It
  survives a crash, watchdog or software restart, but not a power cut.
* After a crash, watchdog reset or brownout, every chat gets "Unexpected restart" with the reason, how long the board
  had been running, the crash address and backtrace, and the log from before the restart as a file.
* Each release carries `Solar2MQTT_ttgo_tdisplay_telegram_V<version>.elf.gz` (debug symbols). To decode a backtrace:
  `xtensa-esp32-elf-addr2line -pfiaC -e firmware.elf <addresses>`.
* Test builds with `-DDIAG_CRASH_TEST` crash on purpose when `crashtest` is typed in the web serial console.

## Dashboard (Telegram Mini App)

The summary's 📊 Dashboard button opens `dashboard/index.html` (repository root), served by GitHub Pages
(`custom_dashboard_url` in platformio.ini, build flag `DASHBOARD_URL`), inside Telegram: grid and battery tiles (the
battery tile reads "53.5 V · Float · ⬆ 7 A": voltage, then the charging stage from link key `cs` — 1 bulk, 2 absorb,
3 float, left out while nothing is charging, from `Inverter_Charge_State` — then the charge or discharge current), the
power flow panel (with the battery discharge time while the grid is off), warnings, a 24 h chart of battery % and
load % (bars coloured by grid state: on / partly off / off) with today's usage and outages under it, and an
ℹ️ Info panel carrying every parameter the link holds.

* ⚙️ Settings panel (inside Telegram, private chat only): grid on/off, power alert with its threshold, battery alerts,
  solar panels connected, battery capacity, cut-off, learn from outages (with what has been
  learned and "Forget"; own use and efficiency have no rows, the stored 40 W / 95 % stand in until learned), a live preview of the discharge time at the current load,
  the inverter's output / charger priority (grid charging is read-only, see "Inverter settings from the Dashboard"), and "Reset to defaults" (alerts and model values;
  capacity, solar and the inverter's settings stay). The link carries the current values
  (`cfg=s3_<flags hex>_<Wh>_<reserve %>_<full %>_<idle W>_<efficiency %>_<alert, 100 W>`, flag bits 2 grid, 4 power,
  8 battery, 32 solar, 64 learn, 128 forget once), the learned values (`lh` hours, `lc` Wh, `le` %, `li` W), the
  inverter's settings (`io`, `ic`, `iu`, allowed amps `il`) and the bot's username (`bu`). Save may append
  `_<output>_<charger>_<amps>` (each a number or `x` for unchanged). `s1` and `s2` codes from older pages are still
  accepted, and the page answers older firmware in its own format. The battery ring and the T-Display battery bar are complete at the "battery full" level
  (`device.batteryFullPct`, default 100 %, link key `bf`), the page always saves 100 % (the float voltage decides how full the battery gets). Save opens `t.me/<bot>?start=<code>`, so
  Telegram sends `/start <code>` from the user's chat; the board validates and applies it (only from a paired chat),
  deletes that message and edits the summary a few seconds later so the Dashboard link carries the new values.

* ℹ️ Info panel (after Settings, foldable, read-only, with the same segmented tabs): 🔌 Board and ⚡ Inverter, listing
  every parameter the link already carries - it needs **no new link keys**, so it adds nothing to a URL that degrades
  by halving history and alerts when Telegram rejects it. Board: `fw`, `up`, `rs`, `tz`, `ok`/`na`, `wh`,
  `wi`, `we`, the learned set (`lh`, `lc`, `le`, `li`) and `bu`. `wr` and `bf` are still
  sent and still used - the power flow and the battery ring need them - but they are not Info rows: `wr` duplicates
  the inverter's own cut-off (`sc`) and `bf` stopped being a setting. Inverter: `m`, `b`, `bv`, `bc`, `bd`, `cs`, `l`,
  `lp`, `va`, `gv`, `gf`, `ov`, `of`, `tc`, `pv`, `w`, `lr`, `op`, `cp`, `iu`, `it`, `ig`, `vb`, `vf`, `sg`, `sd`,
  `sc`, the `ix` flags and `bms`. Rows with no value are skipped, and the Inverter tab hides itself when the inverter
  did not answer. Values are escaped: `w` and `bms` come from the inverter. It replaced the old four-row panel -
  Today moved into the history card it is computed from, temperature and solar became Inverter rows. Beware the
  falsy trap: `ix=0` is a real value (all four flags off) and must render as "off", not vanish.
  `wireTabs(panelId)` is shared with Settings and scoped to one container, so the two tab strips never move each
  other's panels - verified by clicking both.

* Power flow panel: Grid → Home, Grid → Battery (charging) and Battery → Home (discharging) with moving dots, faster
  for more power. The grid circle's ring fills against the inverter's rated power (AC_Out_Rating_Active_Power, 6 kW
  here): blue, amber above 80 %, red with "over the limit" above 100 %. The inverter does not report grid power, so
  the page estimates it as appliances + (battery charging − solar) ÷ efficiency + own consumption.
* The page is static and gets no data from any server: the board puts the values and the history into the button's
  link after `#` (the fragment, which browsers never send), rebuilt every 2 s and refreshed with every summary edit.
  The page shows the data of the latest summary; reopening the button gives newer values.
* History: 96 slots of 15 minutes (battery % at the end, average load in 25 W steps, minutes without grid) in RTC
  memory, so it survives a crash or a firmware update but not a power cut. The board's clock comes from NTP (UTC); the
  page shows times and "today" in the phone's timezone.
* Time left on battery (dashboard panel, and at the end of the summary's Battery line while on battery, e.g.
  "🔋 Battery    69% ≈ 10h 18m") =
  capacity × (battery % − reserve) ÷ (load ÷ efficiency + own consumption), with the learned values once known (see
  "Learned battery model"), otherwise from the ⚙️ Settings panel: Battery
  capacity [Wh] (hidden while 0), Battery reserve [%] (the inverter's low-battery cut-off, default 10), Inverter own
  consumption [W] (default 40) and Inverter efficiency [%] (default 95); the last two are no longer in the ⚙️ panel.
* Mini App buttons only work in private chats; in a group the button opens the same page in the browser.
* If Telegram rejects the link (too long), the board halves the history in the next link, and drops the button if
  even a link without history is refused ("[Telegram] Dashboard link rejected" in the log).
* One-time setup: GitHub → repository Settings → Pages → Deploy from a branch → the branch, folder `/ (root)`;
  `.nojekyll` makes Pages serve the files as they are. The page is then at `https://<owner>.github.io/<repo>/dashboard/`.
  (The upstream `Docs/` folder has a capital D, which Pages does not accept as its docs folder.)
