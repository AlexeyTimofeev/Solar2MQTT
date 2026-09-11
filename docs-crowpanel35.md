# Elecrow CrowPanel ESP32 3.5" HMI (DIS05035H) — hardware notes for a LovyanGFX port

Board sold on arduino.ua as "3.5 SmartView дисплей HMI ESP32 320x480". Elecrow SKU DIS05035H,
wiki name "CrowPanel ESP32 HMI 3.5-inch Display". Researched 2026-09-10 from web sources only
(no board on hand). Where sources disagree it is called out explicitly.

Method: besides the wiki/GitHub pages, Elecrow's official schematic archive
(`3.5-DIS05035H-SCH&PCB.zip`, Eagle `.sch`/`.brd` XML for V2.0/V2.1/V2.2) was downloaded and parsed
net-by-net, so pin numbers below marked **[SCH]** come from the V2.2 schematic itself
(`CrowPanel ESP32 Display-3.5-V2.2-20240704.sch`), not from copied tables.

Primary sources (short names used below):

| Tag | Source |
|---|---|
| WIKI | https://www.elecrow.com/wiki/esp32-display-352727-intelligent-touch-screen-wi-fi26ble-320480-hmi-display.html |
| PROD | https://www.elecrow.com/esp32-display-3-5-inch-hmi-display-spi-tft-lcd-touch-screen.html |
| GH | https://github.com/Elecrow-RD/CrowPanel-3.5-HMI-ESP32-Display-480x320 (branch `master`) |
| GH-V22NOTES | `Eagle_SCH&PCB/V2.2/readme.md` in GH ("CrowPanel ESP32 Display 3.5 V2.2 Product Hardware Driver Notes", Elecrow, 2026-07-29) |
| SCH | https://www.elecrow.com/download/product/CrowPanel/ESP32-HMI/3.5-DIS05035H/3.5-DIS05035H-SCH&PCB.zip |
| FORUM-HIST | https://forum.elecrow.com/discussion/672/esp32-display-update-history |
| MANUAL | https://www.elecrow.com/download/product/ESP32_Display/ESP32_Display_HMI_User_Manual.pdf |
| LCDSPEC | https://www.elecrow.com/download/product/ESP32_Display/3.5inch/QD354801_Specification.pdf |
| COURSE | https://github.com/Elecrow-RD/CrowPanel-ESP32-Display-Course-File |
| ESPHOME-EL | `example/V2.2/ESPHome/basic-hmi-35inch.yaml` in GH |
| ESPHOME-HA | https://community.home-assistant.io/t/dis05035h-elecrow-crowpanel-3-5-config/783983 |
| ESPHOME-RE | https://github.com/RyanEwen/esphome-lvgl/blob/main/devices/DIS05035H.yaml |
| CIRCUITPY | https://github.com/adafruit/circuitpython/tree/main/ports/espressif/boards/elecrow_crowpanel_3.5 (`pins.c`, `board.c`, `mpconfigboard.mk`, `mpconfigboard.h`) |
| OPENHASP | https://www.openhasp.com/0.7.0/hardware/elecrow/crowpanel-hmi-spi/ and https://github.com/HASwitchPlate/openHASP/discussions/801 |
| YORADIO | https://github.com/e2002/yoradio/issues/186 |
| FORUM-1061 | https://forum.elecrow.com/discussion/1061/esp32-3-5-inch-display-touch-examples-not-working |
| FORUM-1138 | https://forum.elecrow.com/discussion/1138/dis05035h-crowpanel-3-5-hmi-esp32-display-not-turning-on |
| MAKERGUIDES | https://www.makerguides.com/digital-clock-crowpanel-3-5-display/ |
| LGFX | https://github.com/lovyan03/LovyanGFX (`src/lgfx/v1/panel/Panel_LCD.hpp`, `Panel_ILI948x.hpp`, `Panel_Device.cpp`, `touch/Touch_XPT2046.cpp`), issue https://github.com/lovyan03/LovyanGFX/issues/600 |
| TFTESPI | https://github.com/Bodmer/TFT_eSPI/blob/master/TFT_Drivers/ILI9488_Rotation.h |

---

## 0. One-screen summary (latest hardware, V2.2)

| Function | GPIO | Notes |
|---|---|---|
| TFT MOSI (panel "SDA") | 13 | shared with XPT2046 DIN [SCH] |
| TFT SCLK | 14 | shared with XPT2046 DCLK [SCH] |
| TFT CS | 15 | [SCH] |
| TFT DC (panel "DCX"/RS) | 2 | [SCH] |
| TFT RST | none (-1) | panel RESET wired to ESP32 EN through R46 = 0R [SCH] |
| TFT MISO | 33 | **only the XPT2046 DOUT** is on it; the ILI9488 SDO pin is not connected [SCH] |
| Backlight | 27 | GPIO → R38 10k pull-down → gate of 2N7002 N-MOSFET, low-side on LED cathodes → **active HIGH** [SCH] |
| Touch CS | 12 | XPT2046 CS; GPIO12 is the MTDI strapping pin, board has R40 "10K/NC" pull-down [SCH] |
| Touch IRQ | 36 | XPT2046 PENIRQ, R10 10k pull-up to 3V3, input-only pin [SCH] |
| I2C header (HY2.0-4P "I2C") | SDA 22, SCL 21 | 1k pull-ups R27/R26 to 3V3 [SCH] (note: reversed vs Arduino default 21/22) |
| GPIO header (HY2.0-4P "GPIO_D") | 25, 32 | 1k pull-ups R18/R25 to 3V3 [SCH] |
| UART header (HY2.0-4P, silk "UART0", PROD calls it "UART1") | RX = 3, TX = 1 | **UART0, hard-wired in parallel with the CH340C USB-serial** via 22R [SCH] |
| Speaker (PH2.0-2P) | 26 (DAC2) | R12 4.7k → SC8002B bridge amp → VO1/VO2 [SCH] |
| SD/TF (SPI) | MOSI 23, MISO 19, SCK 18, CS 5 | VSPI pins, 10k pull-ups [SCH] |
| Battery (PH2.0-2P) | — | BAT+; TP4054 ("4054A") charger, PROG 2k → ~500 mA; no ADC sense [SCH] |
| Buttons | BOOT = GPIO0, RESET = EN | [SCH] |
| Unused / test pads | 4, 34, 35, 39 | nets "IOx_PROT_A/D", no connector [SCH] |

V2.0/V2.1 differ only in: TFT MISO = **12**, Touch CS = **33** (GH, WIKI, `Changes-2.0vs2.2.txt` in SCH zip).

---

## 1. Hardware revisions

Version table from GH `readme.md`:

| Hardware | Software | Status |
|---|---|---|
| V1.0 | V1.0 | old |
| V1.1 | V1.0 | old |
| V2.0 | V1.0 | old |
| V2.2 | V2.2 | latest |

Schematic files present in SCH zip / GH `Eagle_SCH&PCB`: V1.0 (`WZ3248R035-ESP32-V1.0-20230309`),
V2.0 (`DIS05035H-ESP32-V2.0-20231208`), V2.1 (`CrowPanel ESP32 Display-3.5-V2.1-SCH-20240312`),
V2.2 (`CrowPanel ESP32 Display-3.5-V2.2-20240704`).

What changed (FORUM-HIST, GH readme, SCH):

* **V1.0 → V2.0**: "Version 2.0 upgrades the automatic download feature from Version 1.0" (GH readme);
  FORUM-HIST (Feb 2024): all sizes got battery charging circuits, auto-download (DTR/RTS via two S9013
  transistors, no need to hold BOOT), auto-run after flashing.
* **Feb 2024 (FORUM-HIST)**: "For 3.5-inch display, the chip is changed from ESP32-WROOM-32-N4 to
  ESP32-WROVER-B, PSRAM is changes to 8MB". See §2 for the mess this created in the docs.
* **V2.0 → V2.2** (SCH `Changes-2.0vs2.2.txt`, verbatim): "The main difference is the touch driver chip
  XPT2046 / Version 2.0 TFT_MISO is pin 12; Touch_CS is pin 33. / Version 2.2: TFT_MISO is pin 33;
  Touch_CS is pin 12." The wiki phrase "the touch drive IC is different" is misleading: **both V2.0 and
  V2.2 schematics have the same U3 = XP2046 (XPT2046 clone, SSOP16)**; only the two GPIOs are swapped
  [SCH]. No revision uses a capacitive FT6236/FT6336 — that is the different "ESP32 Terminal 3.5" product.
* V2.1: schematic PDF only; same netlist as V2.2 for the ESP32 pins (dated 2024-03-12, i.e. right after
  the WROVER switch). Elecrow groups it with V2.0 in `example/V1.0_and_V2.0_and_V2.1` (pins 12/33).

How to tell which one you have: silkscreen on the back reads "V2.0"/"V2.2" next to the ESP32 module
(visible in the WIKI pinout render). If touch is dead with 12/33, try 33/12.

Probable reason for the swap (inference, not stated by Elecrow): on V2.0 the XPT2046 DOUT sits on
GPIO12 (MTDI strapping pin, selects flash voltage at reset). Touch CS (GPIO33) floats at reset, so the
XPT2046 can drive DOUT high and brick the boot. V2.2 puts CS on GPIO12 (pulled low, harmless) and DOUT
on GPIO33.

---

## 2. ESP32 module, flash, PSRAM — sources disagree

| Source | Module | Flash | PSRAM |
|---|---|---|---|
| WIKI spec table, GH readme, PROD spec table, MAKERGUIDES, OPENHASP | ESP32-WROVER-B | 4 MB (OPENHASP) | 8 MB (OPENHASP) |
| WIKI/PROD intro paragraph | "ESP32-WROOM-32" | — | — |
| MANUAL p.6 (3.5" column) | ESP32-WROOM-32-N4 | 4 MB | "/" (none) |
| SCH V2.0 and V2.2 (symbol + note "主控 ESP32-WROOM-32-N4") | WROOM-32-N4 symbol; V2.2 **.brd value = "ESP32-WROVER-B"**, footprint still the WROOM 18×25.5 mm outline | — | — |
| GH-V22NOTES | "ESP32-WROVER-B module (schematic reference designator U24; the device library name still carries ESP32-WROOM-32-N4)" | — | — |
| FORUM-HIST (Feb 2024) | "changed from ESP32-WROOM-32-N4 to ESP32-WROVER-B, PSRAM is changes to 8MB" | — | 8 MB |
| CIRCUITPY `mpconfigboard.mk` | — | `CIRCUITPY_ESP_FLASH_SIZE = 4MB` | `CIRCUITPY_ESP_PSRAM_SIZE = 8MB`, qio 80 MHz |
| GH `example/V2.2/ESP_IDF/sdkconfig.defaults` | "CrowPanel ESP32-WROVER external PSRAM" | — | `CONFIG_SPIRAM=y`, quad, 40 MHz |
| ESPHOME-EL | `board: esp32dev`, `-DBOARD_HAS_PSRAM`, `psram: mode: quad` | — | yes |
| GH `example/V2.2/PlatformIO35/platformio.ini` | `board = denky32` ("Denky32 (WROOM32)", 4 MB, no PSRAM flags) | 4 MB | not enabled |
| GH `example/V2.2/Micropython/firmware3.5.bin` | strings: "MicroPython v1.20.0-700 … ESP32 module with ESP32" = the generic non-SPIRAM build | — | not used |

Reading of the evidence: boards built since Feb 2024 (V2.1 and V2.2) carry an **ESP32-WROVER-B
(4 MB flash, 8 MB PSRAM)**; V1.x and early V2.0 boards carried a WROOM-32-N4 (4 MB, no PSRAM).
Elecrow never updated the schematic symbol, the user manual or the marketing intro. Flash is 4 MB in
every source. Recommendation for firmware: build with `-DBOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue`
(Arduino-ESP32 boots without PSRAM when it is absent, it only logs an error), keep frame buffers in
internal RAM or size them at runtime after `psramFound()`, use a 4 MB partition table.
WROVER-B does not bring out GPIO16/17 (used by the PSRAM); the schematic leaves them unconnected anyway.

---

## 3. Display

* Panel: QD354801 module, 3.5" TN, 320(H)×480(V) RGB, driver **ILI9488**, "Normally Black", viewing
  direction 12 o'clock, backlight "White LED*6" (LCDSPEC). WIKI calls it "ILI9488V".
* Interface: 4-wire SPI (SCL, SDA, DCX, CSX). IM0/IM1/IM2 each pulled to 3V3 via 1k (R50/R36/R35) [SCH].
  DB0–DB17 tied together to R44 (0R/NC) to GND. TE and RD go to test pads only.
* **SDO not connected**: the connector symbol has an SDO pin, but no net is attached to it in V2.0 or
  V2.2 [SCH]. Net `IO33_TFT_SDO/TP_OUT` only joins GPIO33 and U3.DOUT. Display read-back (ID, pixels)
  is impossible → LovyanGFX `cfg.readable = false`. The TFT_eSPI warning about the ILI9488 SDO not
  tri-stating does not apply here.
* RESET: `TFT_RESET` net = R46 (0R) to `EN_RESET`, plus optional R47 (NC/1k) to 3V3 and C23 (4.7uF/NC)
  [SCH]. The panel resets with the ESP32 EN line (reset button, CH340 RTS auto-reset). No GPIO → `pin_rst = -1`.
  GH-V22NOTES: "RESET=-1, meaning TFT RESET is hardware-linked to the board-level EN_RESET".
* Backlight: LEDA = 3V3; six cathodes LEDK..LEDK6 via 3.9R each (R7, R11, R28, R29, R32, R33) join net
  LEDK = drain of Q6 (2N7002); source = GND; gate = GPIO27 with R38 10k to GND [SCH]. Active HIGH,
  default off at reset. Elecrow's User_Setup: `#define TFT_BACKLIGHT_ON HIGH`; CIRCUITPY `board.c`:
  `backlight_on_high = true`, PWM 50 kHz. LCDSPEC backlight: IF typ 90 mA / max 120 mA, VF typ 3.2 V.
  PWM dimming on GPIO27 works (ESPHome `ledc`, GH-V22NOTES suggests 10–20 kHz).
* Colour: TN, no inversion (`invert_colors: false` in all ESPHome configs; CIRCUITPY sends no INVON;
  TFT_eSPI default). Colour order BGR (`TFT_MAD_BGR` in TFT_eSPI, `color_order: bgr` in ESPHOME-EL).
  Over SPI the ILI9488 needs 18-bit (3 bytes/pixel) writes; LovyanGFX `Panel_ILI9488` forces
  `rgb888_3Byte` automatically when the bus is SPI (LGFX `Panel_ILI948x.hpp`).
* Native resolution: 320 wide × 480 high (portrait). `TFT_WIDTH 320`, `TFT_HEIGHT 480` (Elecrow
  User_Setup); CIRCUITPY 320×480 rotation 0; ESPHome `dimensions: height 480, width 320`.
* SPI clock used by Elecrow: 15.999 MHz (Arduino demos, `SPI_READ_FREQUENCY 20000000`), 27 MHz
  (V2.2 ESP-IDF `sdkconfig.defaults`), 60 MHz in the "TFT_eSPI for v2.2" User_Setup (works for them);
  ESPHome/CircuitPython 20 MHz. ILI9488 datasheet minimum write cycle is 50 ns (20 MHz); 40 MHz is the
  common practical ceiling. Pins 12/13/14/15 are the HSPI IOMUX pins → use `SPI2_HOST`.

### 3.1 Rotation — which LovyanGFX value gives landscape with the USB-C on a given side

Physical layout (WIKI pinout render, GH product photo, SCH `.brd` coordinates): board 101.5 × 59.6 mm.
On the component (back) side the USB-C, BOOT/RESET buttons and BAT connector are on the left short
edge and the three HY2.0 headers are on the top long edge. Seen from the front (LCD side) that mirrors
to: **USB-C on the right short edge, HY2.0 headers along the top edge, speaker/TF at the bottom edge**.
The LCD flex enters on the same (right) edge. Elecrow's product photo shows the factory landscape UI
upright in exactly this orientation.

All Elecrow landscape examples (`LVGL_Arduino3.5.ino`, Squareline demo, `Draw.ino`, `Paint.ino`,
V2.2 PlatformIO `main.cpp`, V2.2 IDF `main.cpp`, Example5) use TFT_eSPI `setRotation(1)` with 480×320.
⇒ **TFT_eSPI rotation 1 = landscape, USB-C on the right, headers on top; rotation 3 = USB-C on the
left.** (High confidence, but only verifiable on hardware; the factory firmware source is not published.)
USB-C "at the bottom" is only possible in portrait (rotation 0 or 2, see below).

**LovyanGFX does not use the same MADCTL table as TFT_eSPI for the ILI9488.** Verified in source:

* TFT_eSPI `ILI9488_Rotation.h`: r0 = `MX|BGR`, r1 = `MV|BGR`, r2 = `MY|BGR`, r3 = `MX|MY|MV|BGR`
  (r4..7 = the same with MX toggled).
* LovyanGFX `Panel_LCD::getMadCtl` default table: r0 = `0`, r1 = `MV|MX|MH`, r2 = `MX|MH|MY|ML`,
  r3 = `MV|MY|ML`, r4 = `MY|ML`, r5 = `MV`, r6 = `MX|MH`, r7 = `MV|MX|MY|MH|ML`.
  `Panel_ILI9488` (in `Panel_ILI948x.hpp`) does **not** override it (only `Panel_ILI9481` does).
* i.e. LovyanGFX's table is TFT_eSPI's ST7789 table; for the ILI9341/ILI9488 family every entry differs
  by the MX bit. On this panel (ESPHome needs `mirror_x: true` in portrait, TFT_eSPI r0 = MX, CircuitPython
  uses MADCTL 0x80 = MY) MADCTL 0x08 is a mirror image, so plain LovyanGFX rotations 0–3 would show
  **mirrored** text and 4–7 the correct image (exactly the pattern a user reported for an ILI9341 in
  LGFX issue #600: "0..3 mirrored, 4..7 normal"). There is no single `offset_rotation` that maps all four
  TFT_eSPI rotations, because the difference is a reflection, not a rotation.

Two equivalent fixes:

1. Subclass `Panel_ILI9488` and override `getMadCtl()` with TFT_eSPI's table (done in §9). Then
   LovyanGFX `setRotation(n)` == TFT_eSPI `setRotation(n)` for n = 0..3, Elecrow's docs/calibration
   apply 1:1, and LovyanGFX's touch rotation stays consistent (the override toggles MX in every entry,
   which is a fixed mirror of the native frame; `Panel_Device::convertRawXY` composes correctly with it).
   **Landscape, USB-C right → `setRotation(1)`; USB-C left → `setRotation(3)`; portrait with USB-C at the
   bottom → expected `setRotation(0)`, USB-C at the top → `setRotation(2)` (portrait assignment inferred
   from the rotation model; verify).**
2. Keep stock `Panel_ILI9488` and use rotations 6/5/4/7 for TFT_eSPI's 0/1/2/3 (landscape USB-right =
   `setRotation(5)`), with the touch calibration x-axis reversed (x_min/x_max swapped vs §4).

---

## 4. Touch

* Controller: **XPT2046** (marked "XP2046", SSOP16, U3) on every revision (V2.0 and V2.2 schematics,
  WIKI "Touch Drive: XPT2046" datasheet link, SCH title "TP Drive:XPT2046"). Resistive 4-wire, stylus
  included. Not I2C, no FT6x36 on this SKU.
* Bus: **shares the display SPI** (SCLK 14, MOSI/DIN 13). DOUT → GPIO33 (V2.2) / GPIO12 (V2.0).
  CS → GPIO12 (V2.2) / GPIO33 (V2.0). PENIRQ → GPIO36 with R10 10k pull-up (active low). VBAT/IN
  pins: R41 10K/NC to 3V3 (not used) [SCH]. CIRCUITPY `pins.c` comment: "TFT and Touch Panel share the
  same SPI bus … Version 2.2 hardware (Sep 2024) swaps MISO & touch CS pins".
* Elecrow polls (`getTouch(&x,&y,600)`, pressure threshold 600) and never uses the IRQ; ESPHome
  configs use `interrupt_pin: 36`, `threshold: 400`. Touch SPI clock: 600 kHz (Arduino User_Setup),
  2.5 MHz (V2.2 IDF), XPT2046 max 2.5 MHz.
* Raw axis convention: LovyanGFX `Touch_XPT2046` reads X with command 0xD1 and Y with 0x91 — the same
  assignment as TFT_eSPI (0xD0/0x90) and ESPHome, so raw numbers are directly comparable.

Calibration values found:

| Source | Frame | Values |
|---|---|---|
| Elecrow `LVGL_Arduino3.5.ino`, Squareline demo (3.5" block), V2.2 PlatformIO `main.cpp` | TFT_eSPI `setTouch()` after/with `setRotation(1)` (480×320) | `uint16_t calData[5] = { 353, 3568, 269, 3491, 7 };` |
| Elecrow `Example5_Initialize_the_touch.ino` (V2.2 course) | TFT_eSPI rotation 1 | `{557, 3263, 369, 3493, 3}` (this is the 2.8" set from the Squareline demo, reused) |
| ESPHOME-EL (Elecrow, V2.2) | ESPHome `xpt2046` | `x_min: 280  x_max: 3860  y_min: 340  y_max: 3860`, `transform: mirror_x: true`, threshold 400 |
| ESPHOME-HA (V2.2, portrait 320×480, display `mirror_x: true`) | ESPHome | `x_min: 280  x_max: 3860  y_min: 3860  y_max: 340` (no touch transform) |
| ESPHOME-RE (V2.2) | ESPHome | `x 280..3860, y 340..3860`, `transform: mirror_y: true` |

All of these say the same thing once converted to the TFT_eSPI-rotation-0 frame (portrait 320×480,
MADCTL MX): raw X grows with screen x (≈280 at x=0 → ≈3860 at x=319), raw Y **falls** with screen y
(≈3860 at y=0 → ≈340 at y=479). Elecrow's `{353,3568,269,3491,7}` (flags 7 = swap + invert x + invert y
at rotation 1) converts to raw X 269→3491, raw Y 3568→353 in that same frame — same directions, slightly
narrower span (different unit). For the §9 class (rotation 0 == TFT_eSPI rotation 0) that is
`x_min = 280, x_max = 3860, y_min = 3860, y_max = 340`, `offset_rotation = 0`; LovyanGFX accepts
`y_min > y_max` (it fits an affine map from the four raw corners). Treat them as defaults: run
`lcd.calibrateTouch()` once per unit and persist the 8 values in NVS (resistive panels drift).

---

## 5. External connectors

All HY2.0-4P headers: pin 3 = 3V3, pin 4 = GND. **No 5 V is available on any header.**

| Connector | Type | Pins (from [SCH], WIKI table) |
|---|---|---|
| UART0 / "UART1" (J10) | HY2.0-4P | 1 = GPIO3 (ESP RX), 2 = GPIO1 (ESP TX), 3 = 3V3, 4 = GND. Same nets as the CH340C (RXD via R71 22R, TXD via R72 22R). WIKI: "UART RX(IO3); TX(IO1)". |
| I2C (J6) | HY2.0-4P | 1 = GPIO21 SCL, 2 = GPIO22 SDA, 1k pull-ups. WIKI: "SDA(IO22); SCL(IO21)". Elecrow code: `Wire.begin(22, 21)`. |
| GPIO_D (J7) | HY2.0-4P | 1 = GPIO25 (DAC1/ADC2_CH8), 2 = GPIO32 (ADC1_CH4), 1k pull-ups. WIKI: "GPIO_D IO25; IO32". |
| Speaker (J8) | PH2.0-2P | VO1/VO2 of SC8002B bridge amp driven from GPIO26 (DAC2). Bridged output — neither pin is ground. Amp SHUTDOWN tied to AGND (always on). |
| Battery (J5) | PH2.0-2P | BAT+ / GND, 3.7–4.2 V Li-ion (PROD). TP4054 charger from VBUS, PROG 2k (≈500 mA, "500mA" note on V2.0 schematic). |
| USB-C (J3) | 16-pin | VBUS → D3 (S2M Schottky) → VIN; CC1/CC2 5.1k pull-downs (C-to-C cables work); D+/D- via 22R to CH340C. |
| TF card (SD1) | micro-SD | GPIO23 MOSI/CMD, GPIO19 MISO/DAT0, GPIO18 SCK, GPIO5 CS/DAT3; 10k pull-ups on CS/MOSI/MISO/DAT1/DAT2. Elecrow: `SPI.begin(18,19,23); SD.begin(5);` |

There is **no second UART header**: PROD/MANUAL list "1×UART0, 1×UART1", but the schematic has one
HY2.0 UART connector on GPIO1/3 (schematic note "UART1接口", silkscreen "UART0"). GH-V22NOTES: "Expansion
UART J10 | RX3, TX1 … Conflicts with download/log UART … GPS may drive simultaneously with CH340, bus
contention risk". For a device link (inverter) prefer a GPIO-matrix UART on GPIO25/32 (GPIO_D header)
or GPIO21/22 (I2C header) and keep UART0 for USB flashing/logging.

Pins shared with USB-serial: GPIO1 (TX0), GPIO3 (RX0); GPIO0 and EN are driven by the CH340C DTR/RTS
auto-reset circuit (Q9/Q10 S9013) — opening a serial monitor resets the board.

---

## 6. Power and gotchas

Power tree [SCH]: VBUS(5 V) → D3 → VIN; BAT+ → Q3 (AO3401 P-MOSFET, gate held high by VBUS via
R45 1k / R1 10k, so the battery is disconnected while USB is present) and D5 (1N5817) → VIN;
VIN → U1 RY3420 buck (FB 45.3k/10k → 3.3 V, L4) → 3V3 for everything (ESP32, LCD, backlight, touch,
SD, headers, CH340C). Battery charge current ≈500 mA from USB only. No battery voltage sense.
PROD: "External power supply: DC 5V-2A", battery "3.7–4.2 V". Typical draw (estimate, not published):
ESP32 with Wi-Fi 150–400 mA + backlight ≈90–150 mA on the 3.3 V rail → budget ≈0.3–0.6 A at 5 V.

Gotchas:

* **GPIO12 (MTDI)** is touch CS on V2.2. It must be low at reset (board: internal pull-down + R40 10k/NC
  to GND). Never add a pull-up on the touch CS line; LovyanGFX drives it high only after boot.
  On V2.0 GPIO12 is the XPT2046 DOUT — if a unit fails with "flash read err" at boot, that is why.
* GPIO2 (TFT DC) and GPIO15 (TFT CS) are strapping pins; the LCD only loads them as inputs, so download
  mode works, but do not add external pull-ups/pull-downs there. GPIO5 (SD CS) has a 10k pull-up (fine).
* **Backlight is off at reset** (10k pull-down on GPIO27). A dark screen with the PWR LED on is almost
  always "backlight never enabled / wrong User_Setup" (FORUM-1138, FORUM-1061).
* No ILI9488 read-back (SDO unconnected) → skip `readPixel`, ID probes and LGFX autodetect.
* Panel reset is EN — LovyanGFX's software reset (0x01) is all you get; keep `pin_rst = -1`.
* I2C header is SDA=22/SCL=21 — reversed from the ESP32 Arduino default (`Wire.begin(22, 21)`).
* GPIO25/32/21/22 have on-board 1k pull-ups to 3V3; fine for UART/I2C, remember it for ADC/inputs.
* GPIO25 and GPIO26 are ADC2 → unusable as ADC while Wi-Fi is on; GPIO32 (ADC1_CH4) is fine.
  GPIO36/39 are input-only.
* Speaker amp is always enabled (SHUTDOWN grounded); drive GPIO26 with the DAC, not digital PWM.
* SD card uses the VSPI pins; keep the display on HSPI (`SPI2_HOST`) so the Arduino `SPI`/`SD`
  objects (VSPI by default) do not collide.
* CH340C and any device on the UART header both drive GPIO3.

---

## 7. Existing configurations found

### 7.1 Elecrow TFT_eSPI `User_Setup.h` (verbatim active defines)

V2.2 — GH `example/V1.0_and_V2.0_and_V2.1/Arduino/Arduino_Tutorial_35/libraries/TFT_eSPI for v2.2/User_Setup.h`
(same file shipped in WIKI download):

```c
#define ILI9488_DRIVER     // WARNING: Do not connect ILI9488 display SDO to MISO if other devices share the SPI bus (TFT SDO does NOT tristate when CS is high)
#define TFT_WIDTH  320
#define TFT_HEIGHT 480 // ST7789 240 x 320
#define TFT_BL   27            // LED back-light control pin
#define TFT_MISO 33
#define TFT_MOSI 13 // In some display driver board, it might be written as "SDA" and so on.
#define TFT_SCLK 14
#define TFT_CS   15  // Chip select control pin
#define TFT_DC   2  // Data Command control pin
#define TFT_RST  -1  // Reset pin (could connect to Arduino RESET pin)
#define TFT_BL   27  // LED back-light
#define TOUCH_CS 12     // Chip select pin (T_CS) of touch screen
#define LOAD_GLCD ... #define SMOOTH_FONT
#define SPI_FREQUENCY  60000000//15999999
#define SPI_READ_FREQUENCY  70000000
#define SPI_TOUCH_FREQUENCY  600000
```

V1.0/V2.0/V2.1 — `3.5inch_Squareline_Demo/User_Setup/User_Setup.h` (also in WIKI `HomeAssistant_35.zip`)
differs only in:

```c
#define TFT_BACKLIGHT_ON HIGH  // Level to turn ON back-light (HIGH or LOW)
#define TFT_MISO 12
#define TOUCH_CS 33     // Chip select pin (T_CS) of touch screen
#define SPI_FREQUENCY  15999999
#define SPI_READ_FREQUENCY  20000000
```

WIKI pin text (verbatim): `TFT_MISO 12 / TFT_MOSI 13 / TFT_SCLK 14 / TFT_CS 15 / TFT_DC 2 / TFT_RST -1 /
TFT_BL 27 / TOUCH_CS 33` … "For the latest v2.2 board, the following modifications need to be made:
#define TFT_MISO 33 #define TOUCH_CS 12".

V2.2 ESP-IDF `sdkconfig.defaults` (GH `example/V2.2/ESP_IDF`): `CONFIG_TFT_ILI9488_DRIVER=y`,
`CONFIG_TFT_MISO=33 MOSI=13 SCLK=14 CS=15 DC=2 RST=-1`, `CONFIG_TFT_SPI_FREQUENCY=27000000`,
`CONFIG_TFT_BL=27`, `CONFIG_TFT_BACKLIGHT_ON_HIGH=y`, `CONFIG_TOUCH_CS=12`, `CONFIG_SPI_TOUCH_FREQUENCY=2500000`,
`CONFIG_SPIRAM=y`.

### 7.2 ESPHome (Elecrow V2.2 `basic-hmi-35inch.yaml`, verbatim hardware parts)

```yaml
esp32: { board: esp32dev, framework: { type: arduino } }
psram: { mode: quad, speed: 80MHz }        # plus platformio build_flags "-DBOARD_HAS_PSRAM"
i2c: { sda: GPIO22, scl: GPIO21 }
spi: [ { id: spi_bus, clk_pin: GPIO14, mosi_pin: GPIO13, miso_pin: GPIO33, interface: hardware } ]
output: [ { platform: ledc, pin: GPIO27, inverted: False, id: backlight_pwm } ]
touchscreen:
  platform: xpt2046
  cs_pin: GPIO12            #interrupt_pin: GPIO36 (commented out)
  threshold: 400
  calibration: { x_min: 280, x_max: 3860, y_min: 340, y_max: 3860 }
  transform: { mirror_x: true, mirror_y: false, swap_xy: false }
display:
  - platform: ili9xxx
    model: ILI9488_A
    data_rate: 20MHz
    cs_pin: GPIO15
    dc_pin: GPIO2
    reset_pin: GPIO4          # GPIO4 is an unconnected pad on this board; harmless
    dimensions: 480x320
    invert_colors: false
    color_order: bgr
```

Community V2.2 configs (ESPHOME-HA, ESPHOME-RE): `board: odroid_esp32`, `miso_pin: 33 #12 on v2.0`,
display `ILI9488_A`, `dimensions: height 480 width 320`, `transform: mirror_x: true`, `cs 15`, `dc 2`,
`data_rate: 20MHz`; touch `xpt2046` `cs_pin: 12 #33 on v2.0`, `interrupt_pin: 36`, `threshold: 400`,
calibration `x 280..3860`, `y 3860..340` (HA) / `y 340..3860 + mirror_y` (RE).
Note the Elecrow-run repo `Elecrow-RD/CrowPanel_for_ESPHome` only ships 4.3/5/7" YAMLs (issue #1 asks for the 3.5").

### 7.3 CircuitPython board definition (CIRCUITPY `pins.c`, verbatim excerpt)

```c
    // TFT and Touch Panel share the same SPI bus
    { MP_ROM_QSTR(MP_QSTR_TFT_MISO), MP_ROM_PTR(&pin_GPIO12) },
    { MP_ROM_QSTR(MP_QSTR_TFT_MOSI), MP_ROM_PTR(&pin_GPIO13) },
    { MP_ROM_QSTR(MP_QSTR_TFT_CLK), MP_ROM_PTR(&pin_GPIO14) },
    { MP_ROM_QSTR(MP_QSTR_TFT_CS), MP_ROM_PTR(&pin_GPIO15) },
    { MP_ROM_QSTR(MP_QSTR_TFT_DC), MP_ROM_PTR(&pin_GPIO2) },
    { MP_ROM_QSTR(MP_QSTR_TFT_BACKLIGHT), MP_ROM_PTR(&pin_GPIO27) },
    { MP_ROM_QSTR(MP_QSTR_TP_CS), MP_ROM_PTR(&pin_GPIO33) },
    { MP_ROM_QSTR(MP_QSTR_TP_IRQ), MP_ROM_PTR(&pin_GPIO36) },
    // Version 2.2 hardware (Sep 2024) swaps MISO & touch CS pins
    { MP_ROM_QSTR(MP_QSTR_TFT_MISO_ALT), MP_ROM_PTR(&pin_GPIO33) },
    { MP_ROM_QSTR(MP_QSTR_TP_CS_ALT), MP_ROM_PTR(&pin_GPIO12) },
    // SD card SPI bus
    { MP_ROM_QSTR(MP_QSTR_MISO), MP_ROM_PTR(&pin_GPIO19) },
    { MP_ROM_QSTR(MP_QSTR_MOSI), MP_ROM_PTR(&pin_GPIO23) },
    { MP_ROM_QSTR(MP_QSTR_SCK), MP_ROM_PTR(&pin_GPIO18) },
    { MP_ROM_QSTR(MP_QSTR_SD_CS), MP_ROM_PTR(&pin_GPIO5) },
    { MP_ROM_QSTR(MP_QSTR_GPIO1), MP_ROM_PTR(&pin_GPIO25) },
    { MP_ROM_QSTR(MP_QSTR_GPIO2), MP_ROM_PTR(&pin_GPIO32) },
    { MP_ROM_QSTR(MP_QSTR_SPEAK), MP_ROM_PTR(&pin_GPIO26) },
```
`board.c`: SPI 20 MHz, MADCTL `0x36, 1, ROTATION_Y_FLIP` (0x80), COLMOD 18-bit, backlight GPIO27
`backlight_on_high = true`, 320×480 rotation 0. `mpconfigboard.h`: "doesn't have USB by default, it
instead uses a CH340C USB-to-Serial chip", console UART TX GPIO1 / RX GPIO3.

### 7.4 LovyanGFX

No LovyanGFX configuration for DIS05035H was found anywhere: not in the LovyanGFX repo (issue/PR
search for "crowpanel" returns unrelated hits), not in openHASP (its LovyanGFX driver has only
`user_setups/esp32s3/crowpanel-hmi.ini` for the S3 boards; the docs list the 3.5" but discussion #801
confirms there is no build), not on Sourcegraph/GitHub code search, and Elecrow's own "Lesson 2 Draw GUI
with LovyanGFX / 2.4inch_2.8inch_3.5inch/Draw/Draw.ino" (COURSE) actually `#include <TFT_eSPI.h>` — their
LovyanGFX lessons target the 4.3/5/7" RGB boards. YORADIO (LovyanGFX-free) confirms the V2.2 pinout
"SCK=14, MOSI=13, MISO=33, TFT_CS=15, TFT_DC=2, TS_CS=12" works with TFT_eSPI. The class in §9 is
therefore derived from the schematic + Elecrow's TFT_eSPI setup, not copied from a tested config.

---

## 8. Schematic net list for the ESP32 (V2.2, verbatim net names) [SCH]

```
GPIO0   IO0_BOOT              K1 (BOOT), Q10.C (auto-download)
GPIO1   UART0_TXD0            R71(22R)->CH340C RXD, J10.2
GPIO2   IO2_TFT_RS            J1.DCX
GPIO3   UART0_RXD0            R72(22R)->CH340C TXD, J10.1
GPIO4   IO4_PROT_A/D          (pad only)
GPIO5   IO5_TF_CS             SD1.CD/DATA3, R67 10k->3V3
GPIO12  IO12_TP_CS            U3.CS, R40 10K/NC->GND          (V2.0: IO12_TFT_SDO/TP_OUT = U3.DOUT)
GPIO13  IO13_TFT_SDI/TP_DIN   U3.DIN, J1.SDA, R39 1K/NC->3V3
GPIO14  IO14_TFT_CLK/TP_CLK   U3.DCLK, J1.WRX/SCL, R34 1K/NC->3V3
GPIO15  IO15_TFT_CS           J1.CSX
GPIO18  IO18_TF_SPI_CLK       SD1.SCLK
GPIO19  IO19_TF_SPI_MISO      SD1.DATA0, R24 10k->3V3
GPIO21  IO21_SCL              J6.1, R26 1k->3V3
GPIO22  IO22_SDA              J6.2, R27 1k->3V3
GPIO23  IO23_TF_SPI_MOSI      SD1.CMD, R23 10k->3V3
GPIO25  IO25_PROT_A/D         J7.1, R18 1k->3V3
GPIO26  IO26_SPEAK            R12 4.7k -> SC8002B input network
GPIO27  IO27_LCD_BLK_CTR      Q6.G (2N7002), R38 10k->GND
GPIO32  IO32_PROT_A/D         J7.2, R25 1k->3V3
GPIO33  IO33_TFT_SDO/TP_OUT   U3.DOUT only                    (V2.0: IO33_TP_CS = U3.CS)
GPIO34  IO34_PROT_A/D         (pad only)
GPIO35  IO35_PROT_A/D         (pad only)
GPIO36  IO36_TP_IRQ           U3.PENIRQ, R10 10k->3V3
GPIO39  IO39_PROT_A           (pad only)
EN      EN_RESET              K4 (RESET), R4 10k->3V3, C11, Q9.C, R46 0R -> TFT_RESET
```

---

## 9. Ready-to-paste LovyanGFX v1 device class (V2.2 default, V2.0 switchable)

Targets LovyanGFX 1.1.x/1.2.x on Arduino-ESP32 3.x (PlatformIO). Values marked `// UNVERIFIED` could
not be confirmed without hardware.

```cpp
#pragma once
// Elecrow CrowPanel ESP32 3.5" HMI, SKU DIS05035H ("3.5 SmartView HMI ESP32 320x480").
// Pins from Elecrow schematic "CrowPanel ESP32 Display-3.5-V2.2-20240704" and Elecrow's TFT_eSPI
// User_Setup.h. Define CROWPANEL35_HW_V20 for V1.x/V2.0/V2.1 boards (MISO 12 / touch CS 33).
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#ifndef CROWPANEL35_HW_V20
  #define CP35_PIN_TFT_MISO   33   // V2.2: XPT2046 DOUT only (ILI9488 SDO is not wired)
  #define CP35_PIN_TOUCH_CS   12   // V2.2: MTDI strapping pin, board pull-down keeps it low at reset
#else
  #define CP35_PIN_TFT_MISO   12   // V2.0/V2.1
  #define CP35_PIN_TOUCH_CS   33   // V2.0/V2.1
#endif
#define CP35_PIN_TFT_MOSI     13
#define CP35_PIN_TFT_SCLK     14
#define CP35_PIN_TFT_CS       15
#define CP35_PIN_TFT_DC        2
#define CP35_PIN_TFT_RST      -1   // panel RESET is tied to ESP32 EN (R46 = 0R)
#define CP35_PIN_TFT_BL       27   // 2N7002 low-side switch: HIGH = on, 10k pull-down = off at reset
#define CP35_PIN_TOUCH_IRQ    36   // XPT2046 PENIRQ, 10k pull-up, input-only GPIO

namespace lgfx { inline namespace v1 {

// ILI9488 with TFT_eSPI's MADCTL table, so that setRotation(n) matches Elecrow's TFT_eSPI examples:
//   1 = landscape 480x320, USB-C on the right, HY2.0 headers on top (Elecrow's default "Hor" UI)
//   3 = landscape, USB-C on the left
//   0 / 2 = portrait (expected: 0 = USB-C at the bottom, 2 = at the top)  // UNVERIFIED on hardware
// Stock lgfx::Panel_ILI9488 uses MADCTL 0x00 for rotation 0, which is a mirror image on this panel.
struct Panel_ILI9488_CrowPanel : public Panel_ILI9488
{
protected:
  uint8_t getMadCtl(uint8_t r) const override
  {
    static constexpr uint8_t madctl_table[] =
    {
             MAD_MX|MAD_MH              ,   // 0  (TFT_eSPI 0: MX)
      MAD_MV                            ,   // 1  (TFT_eSPI 1: MV)
                    MAD_MY|MAD_ML       ,   // 2  (TFT_eSPI 2: MY)
      MAD_MV|MAD_MX|MAD_MY|MAD_MH|MAD_ML,   // 3  (TFT_eSPI 3: MX|MY|MV)
             MAD_MX|MAD_MY|MAD_MH|MAD_ML,   // 4  mirrored variants of 0..3
      MAD_MV|MAD_MX|MAD_MH              ,   // 5
                                       0,   // 6
      MAD_MV|       MAD_MY|MAD_ML       ,   // 7
    };
    return madctl_table[r & 7];
  }
};

}} // namespace lgfx::v1

class LGFX_CrowPanel35 : public lgfx::LGFX_Device
{
  lgfx::Panel_ILI9488_CrowPanel _panel;
  lgfx::Bus_SPI                 _bus;
  lgfx::Light_PWM               _light;
  lgfx::Touch_XPT2046           _touch;

public:
  LGFX_CrowPanel35(void)
  {
    { // SPI bus — HSPI/SPI2_HOST: pins 12/13/14/15 are its IOMUX pins; VSPI pins 18/19/23 belong to the SD slot
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;   // Elecrow ships 16 / 27 / 60 MHz setups; drop to 27000000 if you see artifacts  // UNVERIFIED at 40 MHz
      cfg.freq_read   = 16000000;   // irrelevant: no read-back path
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = CP35_PIN_TFT_SCLK;
      cfg.pin_mosi    = CP35_PIN_TFT_MOSI;
      cfg.pin_miso    = CP35_PIN_TFT_MISO;  // needed by the touch controller, not by the panel
      cfg.pin_dc      = CP35_PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    { // Panel
      auto cfg = _panel.config();
      cfg.pin_cs           = CP35_PIN_TFT_CS;
      cfg.pin_rst          = CP35_PIN_TFT_RST;
      cfg.pin_busy         = -1;
      cfg.memory_width     = 320;
      cfg.memory_height    = 480;
      cfg.panel_width      = 320;
      cfg.panel_height     = 480;
      cfg.offset_x         = 0;
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = false;  // ILI9488 SDO is not connected on this board
      cfg.invert           = false;  // TN panel, no inversion (TFT_eSPI / ESPHome / CircuitPython agree)
      cfg.rgb_order        = false;  // BGR (TFT_MAD_BGR in TFT_eSPI, color_order: bgr in ESPHome)
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;   // XPT2046 shares SCLK/MOSI/MISO
      _panel.config(cfg);
    }
    { // Backlight — active HIGH
      auto cfg = _light.config();
      cfg.pin_bl      = CP35_PIN_TFT_BL;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    { // Touch — XPT2046 on the same SPI host
      auto cfg = _touch.config();
      // Raw ADC limits in the rotation-0 (portrait 320x480) frame of the class above.
      // From Elecrow's/community ESPHome V2.2 calibrations (x 280..3860, y 3860..340); Elecrow's TFT_eSPI
      // calData {353,3568,269,3491,7} converts to x 269..3491, y 3568..353 — same directions.
      // Re-calibrate per unit with calibrateTouch() and store the result.       // UNVERIFIED exact span
      cfg.x_min           = 280;
      cfg.x_max           = 3860;
      cfg.y_min           = 3860;   // raw Y decreases towards the bottom of the portrait frame
      cfg.y_max           = 340;
      cfg.pin_int         = CP35_PIN_TOUCH_IRQ;
      cfg.bus_shared      = true;
      cfg.offset_rotation = 0;
      cfg.spi_host        = SPI2_HOST;
      cfg.freq            = 1000000;   // TFT_eSPI uses 600 kHz (Arduino) / 2.5 MHz (IDF); XPT2046 max 2.5 MHz
      cfg.pin_sclk        = CP35_PIN_TFT_SCLK;
      cfg.pin_mosi        = CP35_PIN_TFT_MOSI;
      cfg.pin_miso        = CP35_PIN_TFT_MISO;
      cfg.pin_cs          = CP35_PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

// Usage:
//   LGFX_CrowPanel35 lcd;
//   lcd.init();              // also pulls the backlight high via Light_PWM
//   lcd.setRotation(1);      // landscape, USB-C on the right (Elecrow default)
//   lcd.setBrightness(200);
//   uint16_t cal[8]; if (!loadFromNvs(cal)) { lcd.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 20); saveToNvs(cal); }
//   lcd.setTouchCalibrate(cal);
```

Suggested PlatformIO environment (not from Elecrow; Elecrow's own V2.2 project uses `board = denky32`
+ `bodmer/TFT_eSPI@2.5.31` + `lvgl@9.1.0`):

```ini
[env:crowpanel35]
platform = espressif32            ; or pioarduino for Arduino-ESP32 3.x
board = esp32dev                  ; 4 MB flash; WROVER-B units add 8 MB PSRAM
framework = arduino
board_build.partitions = min_spiffs.csv
build_flags =
  -DBOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue   ; harmless on no-PSRAM (WROOM) units
  -DLGFX_USE_V1
  ; -DCROWPANEL35_HW_V20                            ; only for V1.x/V2.0/V2.1 boards
lib_deps = lovyan03/LovyanGFX@^1.2.0
monitor_speed = 115200
```

## 10. Things that could not be confirmed

* Which rotation maps to "USB-C at the bottom/top" in portrait (0 vs 2) — derived from the rotation
  model, not observed. Landscape "USB-C right = rotation 1" rests on Elecrow's photo plus their
  universal `setRotation(1)`; verify once on hardware.
* Exact touch raw span for a given unit (sources vary 269–353 / 3491–3860); use `calibrateTouch()`.
* Whether every shipped V2.0 unit already has the WROVER-B (the swap happened Feb 2024, V2.0 schematic
  is from Dec 2023) — check `ESP.getPsramSize()` at boot.
* 40 MHz SPI write clock (Elecrow's own configs range 16–60 MHz).
* Total current draw — Elecrow only specifies a 5 V/2 A supply.
