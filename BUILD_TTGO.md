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
| Charger priority | `PCP0n` | 0 Utility first, 1 Solar first, 2 Solar + utility, 3 Only solar (only offered with solar panels connected, since it stops charging from the grid) |
| Grid charging, max | `MUCHGCn` | allowed values from `QMUCHGCR` (2, 10 … 100 A); no zero padding (`MUCHGC60` ACK, `MUCHGC060` NAK); the panel offers it as grid power, 0.5-5 kW in 0.5 kW steps, each sent as the nearest 10 A step at 54 V and 95 % (2 kW is left out: it lands on 40 A like the closer 2.5 kW) |
| Total charging, max | `MNCHGCnnn` | allowed values from `QMCHGCR` (10 … 120 A); three digits (`MNCHGC090` ACK, `MNCHGC0090` NAK); grid charging can not exceed it; hidden while "Solar panels connected" is off, and then set to the grid charging limit (the next allowed value at or above it) whenever that one is changed |
| Grid input range | `PGR0n` | 0 Appliance (wide window), 1 UPS (fast switch-over) |
| Buzzer, overload bypass, restart after overload, restart after overheating | `PEa`/`PDa`, `b`, `u`, `v` | on / off (backlight `x`, grid-loss alarm `y` and power saving `j` never answer on this model) |
| Battery % points (lithium with BMS): back to grid, back to battery, cut-off | `PBCC`, `PBDC`, `PSDC` + `nnn` (3 digits), read with `QDOP` (fields 9-11) | 5-95, 10-100, 0-90 %, any whole % (tested on the VMII-6000: cut-off 5/7/12/15/20/30, back to grid 13/15/20/50, back to battery 83/85/100, each stored as sent); cut-off <= back to grid < back to battery; back to grid / back to battery are shown only in Solar first or Battery first mode (the only modes that use them); `PSDC` answers only after a ~20 s pause |

The panel uses dropdowns that list only accepted values (grid charging in kW mapped to the allowed currents, total charging at its allowed currents,
the % points at whole percent, the voltages in 0.1 V steps; also the power alert and cut-off). The allowed currents are asked once a
minute after start. Apply sends `/start i1_<key><value>_...` with only the
changed settings (Telegram allows 64 characters, too few next to the board's settings): o, c, u, t, g, z buzzer,
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

Lines: ⚙️ Mode, 🔋 Battery as a four-segment bar plus (percent) only (green ≥50 %, yellow ≥25 %, red below), ☀️ Solar
(hidden when solar is switched off), 🔌 Load as a four-segment bar plus (percent) only (green <50 %, yellow <80 %, red
above), 🏠 Grid, 🌡 Temp, ⚠️ alerts,
📶 WiFi (OK at -70 dBm or better, otherwise Low signal), ⏳ Up (uptime), 🕒 Updated, each on its own line with a colon. The device-name header was removed on request.

## High-load summary

Removed in 2.1.17 (the `telegram.loadAlert` setting is gone): the power alert covers it, with the house load above the
threshold while the inverter runs on battery. Battery and Load show one moon-phase glyph for the level (🌑 🌘 🌗 🌖 🌕 for about 0, 25, 50, 75, 100 %, nearest quarter), no colour cue. The Grid line shows voltage only, and WiFi, uptime and Updated are separate lines.

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
(`custom_dashboard_url` in platformio.ini, build flag `DASHBOARD_URL`), inside Telegram: grid and battery tiles, the
power flow panel (with the battery discharge time while the grid is off), warnings, a 24 h chart of battery % and
load % (bars coloured by grid state: on / partly off / off), today's usage and outages, inverter temperature and
board health.

* ⚙️ Settings panel (inside Telegram, private chat only): grid on/off, power alert with its threshold, battery alerts,
  solar panels connected, battery capacity, cut-off, learn from outages (with what has been
  learned and "Forget"; own use and efficiency have no rows, the stored 40 W / 95 % stand in until learned), a live preview of the discharge time at the current load,
  the inverter's output / charger priority and grid charging limit, and "Reset to defaults" (alerts and model values;
  capacity, solar and the inverter's settings stay). The link carries the current values
  (`cfg=s3_<flags hex>_<Wh>_<reserve %>_<full %>_<idle W>_<efficiency %>_<alert, 100 W>`, flag bits 2 grid, 4 power,
  8 battery, 32 solar, 64 learn, 128 forget once), the learned values (`lh` hours, `lc` Wh, `le` %, `li` W), the
  inverter's settings (`io`, `ic`, `iu`, allowed amps `il`) and the bot's username (`bu`). Save may append
  `_<output>_<charger>_<amps>` (each a number or `x` for unchanged). `s1` and `s2` codes from older pages are still
  accepted, and the page answers older firmware in its own format. The battery ring, the summary's moon and the T-Display battery bar are complete at the "battery full" level
  (`device.batteryFullPct`, default 100 %, link key `bf`), the page always saves 100 % (the float voltage decides how full the battery gets). Save opens `t.me/<bot>?start=<code>`, so
  Telegram sends `/start <code>` from the user's chat; the board validates and applies it (only from a paired chat),
  deletes that message and edits the summary a few seconds later so the Dashboard link carries the new values.

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
  "🔋 Battery: 🌖 (69%) ≈ 10h 18m") =
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
