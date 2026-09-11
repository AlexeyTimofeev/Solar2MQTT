# Solar2MQTT for the LilyGO T-Display, with a Telegram bot

This is a personal fork of [softwarecrash/Solar2MQTT](https://github.com/softwarecrash/Solar2MQTT), based on its
V2.0.4 release, for a LilyGO / TTGO T-Display (ESP32 with a 1.14" screen) connected to a PI30 inverter
(tested with a PowMr VMII-6000). All credit for Solar2MQTT goes to its author, and the original licence applies.
For the upstream project, its documentation and other boards, use the original repository.

## What this fork adds

- **Screen pages** on the T-Display: battery, load, solar and status; the two buttons switch pages.
- **Telegram bot**: an inverter summary that keeps itself up to date (edited in place every minute), with the time
  left on battery during a power cut; alerts for low battery, high load and a lost inverter connection; several chat
  IDs; `/summary`, `/restart`; an Upgrade button under the summary when a newer release is out.
- **📊 Dashboard** (Telegram Mini App, the summary's button): battery gauge, load, grid, output, time left on
  battery, 24 h charts of battery and load, a grid on/off strip, today's usage and outages. A static page on GitHub
  Pages; the board puts the data into the button link, so no server is involved.
- **Remote troubleshooting** over Telegram: `/diag` (restart reason, Wi-Fi, memory, inverter link counters), `/log`
  (the recent board log as a file), and after an unexpected restart an automatic report with the crash details and
  the log from before it.
- **More reliable inverter link**: unanswered requests are retried, a short pause lets the inverter resynchronise
  after a run of timeouts, and the connection status tolerates brief gaps. Link counters are in `/api/data`.
- **Battery guard** against the inverter's occasional stray 0 % reading.
- **Settings** for all of the above in the web interface (Telegram page; Device page: solar on/off switch, battery
  capacity and reserve, inverter consumption and efficiency).

## Install and update

Firmware files are on the [Releases](../../releases) page.

- **New board, over USB:** flash `Solar2MQTT_ttgo_tdisplay_telegram_V<version>.bin` at address `0x0`
  (for example with [ESP Web Tool](https://espressif.github.io/esptool-js/) or `esptool.py write_flash 0x0 <file>`).
- **Board already running Solar2MQTT:** upload the `.bin.ota` file on its Firmware page.
- **Later updates:** the board checks this fork's releases; install a new version from its Firmware page or with the
  Upgrade button under a Telegram summary.

After the first start, join the Wi-Fi network `Solar2MQTT-AP` and open `192.168.4.1` to set up Wi-Fi.
Wiring, the Telegram setup and build instructions are in [BUILD_TTGO.md](BUILD_TTGO.md).
Inverter link on the T-Display: GPIO26 (TX) and GPIO27 (RX) through a MAX3232 RS232 level shifter.

---

*The original Solar2MQTT README follows.*

# Solar2MQTT [![GitHub release](https://img.shields.io/github/release/softwarecrash/Solar2MQTT?include_prereleases=&sort=semver&color=blue)](https://github.com/softwarecrash/Solar2MQTT/releases/latest) [![Discord](https://img.shields.io/discord/1007020337482973254?logo=discord&label=Discord)](https://discord.gg/fb2nZWDExz)

# Looking for the ESP8266 Variant? go [HERE](https://github.com/softwarecrash/Solar2MQTT-ESP8266)

# Features:
- Support WiFi or LAN Modules
- captive portal for wifi and MQTT config
- config in webinterface
- Full Controll with [Custom commands](https://github.com/softwarecrash/Solar2MQTT/wiki/Set-parameters)
- get essential data over webinterface, get [all data](https://github.com/softwarecrash/Solar2MQTT/wiki/Datapoints-and-units) over MQTT
- classic MQTT datapoints or Json string over MQTT
- get Json over web at /livejson?
- firmware update over webinterface
- debug log over USB or Webserial
- [blink codes](https://github.com/softwarecrash/Solar2MQTT/wiki/Blink-Codes) for the current state of the ESP
- [Reset functions](https://github.com/softwarecrash/Solar2MQTT/wiki/Reset)
- [Support Home Assistant](https://github.com/softwarecrash/Solar2MQTT/wiki/HomeAssistant-integration)





**works with**
- Most devices that use the watchpower PC Software
-  Most devices that use the Solarpower PC Software
- PIP devices
- i solar 
- IGrid
- Many devices from EASUN
- and many many others based on the chinese solar inverter with a rj45 jack and usb port, primary identified by the display
- Take a look at the [supported devices](https://github.com/softwarecrash/Solar2MQTT/wiki/Supported-Devices) and [reported working devices](https://github.com/softwarecrash/Solar2MQTT/wiki/Reported-Working-Devices) in the wiki
- If your device works, see [how to report a working device](https://github.com/softwarecrash/Solar2MQTT/wiki/Report-a-Working-Device)


**Main screen:**

![alt text](Docs/README/status.png)

**Menu:**

![alt text](Docs/README/menu.png)

**Config:**

![alt text](Docs/README/mqtt.png)
![alt text](Docs/README/device.png)

![alt text](Docs/README/wifi.png)
![alt text](Docs/README/wifi-extendet.png)

![alt text](Docs/README/firmware.png)
![alt text](Docs/README/debug.png)

# How to use:
- flash your ESP32 (recommended Wemos D1 Mini ESP32) with our [Flash2MQTT-Tool](https://flash.2mqtt.de/?get=Solar2MQTT) or our desktop [2MQTT-Flasher](https://github.com/all-solutions/2MQTT-Flasher)
- connect the ESP like the [wiring diagram](https://github.com/softwarecrash/Solar2MQTT/wiki/Wiring-Diagram)
- search for the wifi ap "Solar2MQTT-AP" and connect to it
- surf to 192.168.4.1 and set up your wifi and optional MQTT
- that's it :)

### How-To video by Jarnsen

<a href="http://www.youtube.com/watch?feature=player_embedded&v=7u8hPLdXeso" target="_blank">
 <img src="http://img.youtube.com/vi/7u8hPLdXeso/0.jpg" alt="Watch the video" />
</a>



**POWER:** Using a 3.3V DC Buck Converter that can handle up to 20V or a DC/DC or USB power currently.

# Parts required to build

Most of the parts can be bought as modules, it's usually cheaper that way.

- ESP32
- MAX3232 module Like this https://amzn.eu/d/8t3gk5t or https://bit.ly/3BFPqrw or with orginal cable https://www.amazon.de/dp/B09XWPTDYP
- DC-DC buck module - 12-80v down to 5v

# Completely assembled and tested PCB's

You are welcome to get fully stocked and tested PCB's. These are then already loaded with the lastest firmware. The earnings from the PCBs are used for the further development of existing and new projects.

![Solar-MQTT-PCB](https://user-images.githubusercontent.com/17761850/233859179-cc9c9075-b88a-4f38-b804-bc0f409cf8ce.png)

If interested see [here](https://all-solutions.store)

#
Questions?
[Join the Discord Channel (German / English)](https://discord.gg/pAArqVsVS4)

#
[<img src="https://cdn.buymeacoffee.com/buttons/default-orange.png" alt="Buy Me A Coffee" height="41" width="174"/>](https://donate.softwarecrash.de)

[![LICENSE](https://licensebuttons.net/l/by-nc-nd/4.0/88x31.png)](https://creativecommons.org/licenses/by-nc-nd/4.0/)

