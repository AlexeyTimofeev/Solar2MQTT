# 3.5" "ESP32 LCD TFT Module" (Sunton/JC ESP32-3248S035R / -3248S035C) — hardware notes

Research date: 2026-09-10. Web sources only; values marked **[unconfirmed]** were not cross-checked against a schematic or a board in hand.

Context: AliExpress listing 1005008660264581 ("2.4/2.8/3.5 inch LCD TFT Module ESP32 Arduino LVGL WIFI&Bluetooth Development Board", Stone's Store / brand "AITEWIN ROBOT") could not be loaded (CAPTCHA). The seller's user manual mirrored at manuals.plus is the generic 2.8" ESP32-2432S028R manual (ILI9341, BL on GPIO21) and says nothing 3.5"-specific — https://manuals.plus/ae/1005008660264581 . The 3.5" option in such multi-size listings is the Sunton/Shenzhen Jingcai (JC) ESP32-3248S035 family described below; the factory spec sheet lists exactly two SKUs, ESP32-3248S035R and ESP32-3248S035C (Sunton "ESP32-3248S035 Specifications-EN.pdf", mirrored at https://github.com/lsdlsd88/ESP32-3248S035/tree/main/3.5inch_ESP32-3248S035/2-Specification ).

---

## 1. Variants and how to tell them apart

| Variant | Touch | Touch bus | Notes |
|---|---|---|---|
| ESP32-3248S035R | resistive, XPT2046 | SPI, shared with LCD (HSPI: 14/13/12), CS 33, IRQ 36 | Ships with a stylus; cheaper |
| ESP32-3248S035C | capacitive, GT911 | I2C SDA 33 / SCL 32, RST 25, INT 21 (see §4) | Glass front |
| "N" / no-touch | — | — | Not listed in the factory spec, rzeldent board defs, openHASP or esp3d pages. Some AliExpress listings show a "3.5 inch without touch" option **[unconfirmed]**. Treat as R board minus the touch film; same LCD pins. |

Sources: factory spec PDF (above); rzeldent board table https://github.com/rzeldent/platformio-espressif32-sunton ; openHASP https://www.openhasp.com/0.7.0/hardware/sunton/esp32-3248s035/ ; esp3d https://esp3d.io/esp3d-tft/version_1x/hardware/esp32/sunton-35-3248/

Identifying what a listing ships:
- SKU suffix on the anti-static bag / listing option: "R" = resistive, "C" = capacitive.
- Photos: a stylus in the box or a matte film with a visible air gap over the LCD = R. A glossy glass cover flush with the bezel = C. The C touch panel's flex carries the GT911 chip.
- On the PCB: CN1 pin 2 is **IO22 on the C board and NC on the R board** (ardnew BSP, https://github.com/ardnew/ESP32-3248S035 , "Hardware details" page https://deepwiki.com/ardnew/ESP32-3248S035/7-hardware-details ). ardnew also notes the two variants differ in whether the U4 external flash is populated.
- Board revisions: original boards are micro-USB with CH340C. A newer "Type-C" revision exists: USB-C connector, U4 external flash removed, and a Li-battery charge/discharge/protection IC (FM5324HJ1) — https://github.com/chacuavip10/CYD-3.5inch_ESP32-3248S035 (Docs/Note_TypeC.md, Docs/GPIO_PINOUT_TypeC.png). Case remixes for the USB-C board appeared in Feb 2025 because "no USB-C versions that worked with the board cutouts existed" — https://makerworld.com/en/models/1151020-cyd-3-5-case-esp32-3248s035-usb-c-remix . Connector positions differ between the two revisions; GPIO mapping of the Type-C board was not verified here **[unconfirmed — check GPIO_PINOUT_TypeC.png]**.

Display-controller batches (ILI9486 / ILI9488 vs ST7796):
- Every primary source found says **ST7796** for this board: factory spec ("Driver chip ST7796"), rzeldent JSONs, openHASP ini, esp3d, macsbug, LovyanGFX issue #811, ESPHome/Tasmota threads, Amazon/AliExpress titles. No credible report of an ILI9488 or ILI9486 batch of the 3248S035 was found (searches on GitHub, HA community, Arduino forum). The ILI9488 confusion comes from generic 3.5" SPI TFT modules and from the TFT_eSPI warning that covers both chips (https://github.com/Bodmer/TFT_eSPI/discussions/898).
- Runtime check (MISO is wired, so IDs are readable): read RDID4 (0xD3) — ST7796S returns 3 bytes 0x00 0x77 0x96 after a dummy byte (ST7796S datasheet §9.3.17, https://www.displayfuture.com/Display/datasheet/controller/ST7796s.pdf ); ILI9488 → 0x00 0x94 0x88, ILI9486 → 0x00 0x94 0x86 **[standard driver ID tables; not verified on this board]**. LovyanGFX: `uint32_t id = tft.getPanel()->readCommand(0xD3, 0, 4);` (`readCommand(cmd, index, len)` is public on `Panel_LCD`, reachable through `getPanel()`; https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1/panel/Panel_LCD.hpp ). TFT_eSPI: `tft.readcommand8(0xD3, 1..3)`. Visual: an ILI9488 panel would need `invert`/18-bit-colour changes — if colours come out inverted or garbled with the ST7796 configs in §8, read the ID first.

---

## 2. MCU, flash, PSRAM

| Item | Value | Source |
|---|---|---|
| Module | ESP32-WROOM-32 (chip ESP32-D0WD-V3 per macsbug; openHASP says D0WDQ6), 2×240 MHz | rzeldent README, macsbug, openHASP |
| Flash | 4 MB (module) | all |
| PSRAM | **none** | rzeldent, openHASP, esp3d ("up to 8 MB with hardware mod") |
| Extra flash U4 | W25Q32JV 4 MB on the PCB, CS wired to GPIO11 **in parallel with the module's own flash** — design flaw; macsbug recommends removing U4 (upload failures / corruption). Manuals advertise this as "4MB+4MB Flash". Type-C revision ships without U4. | https://macsbug.wordpress.com/2022/10/02/esp32-3248s035/ , chacuavip10 Note_TypeC.md |
| USB-serial | CH340C (micro-USB; USB-C on new revision). macsbug reports flaky uploads at 921600 — use 460800. | macsbug, esp3d |
| Arduino board | "ESP32 Dev Module", 4 MB, DIO/QIO 80 MHz, PSRAM disabled | macsbug |

---

## 3. Display

| Signal | GPIO | Notes |
|---|---|---|
| MOSI (SDI) | 13 | HSPI / SPI2_HOST |
| MISO (SDO) | 12 | wired (ID/pixel read-back works, `readable=true` in the C config) |
| SCLK | 14 | |
| CS | 15 | |
| DC (RS) | 2 | |
| RST | — (tied to EN; use `pin_rst=-1`) | |
| Backlight | 27 | **active-high** (factory TFT_eSPI `TFT_BACKLIGHT_ON HIGH`; LovyanGFX `Light_PWM invert=false`) |

- Controller ST7796, 320×480 native portrait, 16-bit colour. Colour setup that all working configs use: `invert = false`, `rgb_order = false` (BGR) in LovyanGFX; esp_lcd `COLOR_SPACE_BGR` in rzeldent's defs. (ESPHome users needed `color_order: RGB`, `invert_colors: false` in the ili9xxx driver, whose flag semantics differ — https://community.home-assistant.io/t/help-making-esp32-3248s035-work/748332 .)
- SPI clock: 40 MHz write / 16 MHz read is the conservative LovyanGFX value (macsbug, LovyanGFX #811); the factory LovyanGFX demo runs 80 MHz, the factory/openHASP TFT_eSPI setups 65 MHz.
- Rotation (LovyanGFX `setRotation()`), USB connector on the short edge: **1 = landscape, USB on the right; 3 = landscape, USB on the left**; 0/2 portrait (macsbug). A separate C-board project also uses `setRotation(1)` for landscape (https://github.com/eupherion/ESP32-3248S035-LGFX-NTP-Clock ). The factory capacitive demo comments `setRotation(0); // USB Right` — that comment contradicts the above and looks copy-pasted; trust macsbug.
- rzeldent's LVGL defs express the default portrait orientation as `DISPLAY_SWAP_XY=false, MIRROR_X=true, MIRROR_Y=false` (both R and C JSONs).

Sources: https://raw.githubusercontent.com/rzeldent/platformio-espressif32-sunton/main/esp32-3248S035R.json , …/esp32-3248S035C.json ; factory `User_Setup.h` https://github.com/lsdlsd88/ESP32-3248S035/tree/main/3.5inch_ESP32-3248S035/1-Demo/Demo_Arduino/3_4-6_3.5%20LVGL_Arduino%20Resistive%20touch/TFT_eSPI%20bottom%20layer%20replacement%20file ; openHASP `user_setups/esp32/esp32-3248s035.ini` (ST7796, CS15/DC2/RST-1/BL27, 65/20 MHz, HSPI).

---

## 4. Touch

R — XPT2046 (shared HSPI):

| Signal | GPIO |
|---|---|
| T_CLK / T_DIN / T_DO | 14 / 13 / 12 (shared with LCD) |
| T_CS | 33 |
| T_IRQ | 36 (input-only) |

- SPI clock 1 MHz (LovyanGFX) / 2.5 MHz (TFT_eSPI, openHASP).
- Raw calibration seen in working configs: macsbug `x_min 360, x_max 4200, y_min 180, y_max 3900, offset_rotation 3` (with `setRotation(1|3)`); factory LovyanGFX demo `x 222–3367, y 192–3732, offset_rotation 6` (with `setRotation(0)`); after an S3 transplant macsbug used `420–3900 / 420–3700`. Panels vary — run `tft.calibrateTouch()` once and persist the 8 values.
- IRQ: macsbug found `pin_int = 36` unreliable at 240 MHz (fine at 160 MHz) and recommends `pin_int = -1` (polling). GPIO36/39 pick up glitches while Wi-Fi is active; mitigations `adc_power_acquire()` in setup and `WiFi.setSleep(false)` (macsbug).
- rzeldent: `TOUCH_MIRROR_X=true`, `XPT2046_Z_THRESHOLD=600`.
- ST7796 SDO not tri-stating on a shared bus is a known TFT_eSPI caveat (discussion #898); the shared-bus configs below nevertheless work on this board (`bus_shared = true`).

C — GT911 (I2C):

| Signal | GPIO |
|---|---|
| SDA | 33 |
| SCL | 32 |
| RST | 25 |
| INT | 21 — only through R25 (0 Ω, **not populated**); the GT911 INT trace on the LCM is tied to GND. R18 (10 kΩ) pulls GPIO21 up to 3V3. |
| I2C addr | 0x5D (alt 0x14) |

- Because INT is not usable as shipped, all working configs poll: LovyanGFX `pin_int = -1` (#811), openHASP `TOUCH_IRQ=-1`, esp3d "defaults to polling". esp3d documents the mod (cut INT trace at flex pin 5, bodge to R25/GPIO21, remove R18) — https://esp3d.io/esp3d-tft/version_1x/hardware/esp32/sunton-35-3248/ ; openHASP discussion https://github.com/HASwitchPlate/openHASP/discussions/384 .
- The factory demo configures `pin_int = GPIO_NUM_36`, I2C port 0 at 800 kHz, `x 14–310, y 5–448` (https://github.com/lsdlsd88/ESP32-3248S035/tree/main/3.5inch_ESP32-3248S035/1-Demo/Demo_Arduino/9_2_3.5_LVGL_IOS_Capacitive_touch ) — GPIO36 is not documented as connected to the GT911 anywhere else; prefer `-1`.
- Tasmota template that works: `{"NAME":"esp32-3248S035C","GPIO":[0,1,800,6210,320,1,1,1,672,704,736,768,321,322,1,1,0,0,1,1,0,1,1,992,0,0,0,0,608,640,1,1,1,0,1,1],"FLAG":0,"BASE":1}` with `:UTI,GT911,I1,5d,38,-1` (https://github.com/arendst/Tasmota/discussions/22224 ).

---

## 5. Connectors, free GPIOs, UART + DS18B20 plan

All are JST 1.25 mm (macsbug/esp3d; rzeldent lists the I2C ones as JST 1.0 **[pitch unconfirmed — measure]**).

| Connector | Pins (in order) | Notes |
|---|---|---|
| P1 "power/serial" 4P | VIN (5 V), TX (GPIO1), RX (GPIO3), GND | UART0, shared with CH340 — usable for a device only when USB is not driving the same lines |
| P3 4P | GND, GPIO35, GPIO22, GPIO21 | macsbug (from PCB layout), espboards; esp3d lists the 4th pin as 3V3 **[discrepancy — verify with a meter]** |
| CN1 4P ("temperature & humidity / DHT11") | GND, NC (R) / GPIO22 (C), GPIO21, 3V3 | ardnew: CN1.3 = IO22 on C boards |
| P4 2P | VO1, VO2 | speaker output of the SC8002B/FM8002A amp, driven from GPIO26 (DAC2) |
| TF slot | CS 5, SCLK 18, MISO 19, MOSI 23 | VSPI, separate from the LCD bus |

On-board GPIO use: RGB LED 4 (R), 16, 17 (G/B: rzeldent says G=16 B=17, macsbug says G=17 B=16 — swap if wrong; polarity likely active-low like the 2.8" CYD **[unconfirmed]**); LDR/CDS GT36516 on 34 (ADC1, input-only); speaker 26; boot button 0; GPIO39 not wired (macsbug, ESPHome thread).

ESP32 pin rules (Espressif): GPIO34–39 input-only, no internal pull-ups; strapping pins 0, 2, 5, 12, 15 (all already in use here: 2 = DC, 5 = SD CS, 12 = MISO, 15 = CS); GPIO6–11 = module flash (and U4 on GPIO11). https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gpio.html

Free GPIOs on the connectors (both R and C): **GPIO21, GPIO22** (full I/O) and **GPIO35** (input-only). Nothing else is exposed without soldering (LED pads 4/16/17, boot 0). On the C board GPIO21 carries the 10 k pull-up R18; on the R board GPIO32/25 are unused but only reach the LCM flex pads.

Suggested allocation for a UART pair + one DS18B20 (identical on R and C, everything on P3 + 3V3 from CN1):

| Function | GPIO | Why |
|---|---|---|
| UART2 RX | 35 | input-only is fine for RX; no internal pull-up, so add an external 10 k to 3V3 if the peer can be unplugged |
| UART2 TX | 22 | plain output |
| DS18B20 data | 21 | bidirectional 1-Wire; add 4.7 k to 3V3 (C board already has 10 k R18 — 4.7 k in parallel is fine) |

`Serial2.begin(baud, SERIAL_8N1, /*rx*/35, /*tx*/22);` `OneWire ow(21);`. Alternative: TX 21 / DS18B20 22. GPIO21/22 are the Arduino default `Wire` pins — if you also need I2C, on the C board put I2C devices on the GT911 bus (33/32) instead. Don't use P1 TX/RX while the USB cable is attached.

---

## 6. Power and gotchas

- 5 V in via micro-USB (USB-C on the new revision) or P1 VIN; spec 4.75–5.25 V. Consumption ≈120 mA (user manual), ≈160 mA (factory spec PDF), 140 mA (B2B lister) — budget ≥500 mA. AMS1117-3.3 LDO (androidcrypto). Sources: https://manuals.plus/ae/1005008762308386 , factory spec PDF, https://androidcrypto.github.io/ESP32-Boards/esp32_cyd_st7796_3_5_inches
- U4 external flash in parallel with the module flash → sporadic upload/boot problems; remove U4 or buy the Type-C revision (macsbug, chacuavip10).
- CH340C: use 460800 baud; may need manual BOOT for upload (macsbug).
- No PSRAM: LVGL draw buffers must stay in internal RAM (rzeldent uses 1/16 screen = 38 400 px; a buffer-size fix for the C board landed in esp32-smartdisplay v2.1.0, Nov 2024).
- GT911 INT unusable without a hardware mod → poll (C board). XPT2046 IRQ on GPIO36 unreliable at 240 MHz → poll (R board).
- GPIO36/39 Wi-Fi glitches (see §4); GPIO34 LDR shares ADC1 (usable with Wi-Fi; ADC2 pins are not).
- RGB LED pins 16/17 are the ones a PSRAM mod needs (esp3d) — irrelevant unless you mod.
- SD card is on VSPI (5/18/19/23); touch+SD simultaneously is fine (androidcrypto). The LCD MISO (12) stays on the bus — the ST7796 SDO-not-tristate caveat applies in theory (TFT_eSPI #898).
- Speaker amp input GPIO26 is DAC2; leave it or drive it — nothing else is on it.
- Resistive touch needs calibration; capacitive is 1-point (`CONFIG_ESP_LCD_TOUCH_MAX_POINTS=1` in rzeldent).

---

## 7. Price (2026) and cases

- AliExpress search page (reader snapshot, Sept 2026): 3248S035 boards at **≈US$15–20** ("3248S035 development board" $19.75; "ESP32-3248S035C … ST7796" $15.05, $14.30 for 2+); a $5.55 "3248S035C module" hit is probably a shell/accessory — verify before ordering. Sunton store listing 1005004632953455 has appeared in search results at "12.26 US$" (undated). espboards quotes "$22 typical" (https://www.espboards.dev/esp32/cyd-esp32-3248s035/ ). 2022 Japanese prices were ¥2314 (R) / ¥2733 (C) (macsbug).
- Amazon US listings (prices not retrievable here, expect a premium over AliExpress): DIYmalls B0C4KSKW96 (C), B0F42LTCRQ (C + acrylic case), DIYmall B0C5DB6RHM, AITRIP B0D4VCL6GR (R, mislabelled "C") / B0D4VFCB3N (2-pack), Stemedu B0D879TQ2P, RCmall B0F4XWLDT4, B0F38TGCKC (R/C options). Newegg 3C6-05HR-000B5, 3C6-00S7-009H4.
- Acrylic case: bundled by DIYmalls (Amazon B0F42LTCRQ; eBay 127282849318) and by some AliExpress sellers (aliexpress.us item 3256809024620722 "comes with acrylic case").
- 3D-printed:
  - Printables 739905 "CYD 3.5" Case – esp32-3248S035" by Vanix (C board) https://www.printables.com/model/739905-cyd-35-case-esp32-3248s035
  - Printables 1617281 "… with clips" by ChrisHerman (screwless) https://www.printables.com/model/1617281-cyd-35-case-esp32-3248s035-with-clips
  - Printables 669531 "Wall frame for 3.5" … ESP32-3248S035" by Michal Schwarz https://www.printables.com/model/669531-wall-frame-for-35-lcd-tft-touch-display-esp32-3248
  - Thingiverse 6454470 (Vanixx, 2024-01-27, C board, 60° stand compatible) https://www.thingiverse.com/thing:6454470
  - Thingiverse 6807372 (Botman3D remix, 2024-10-23, holes for all connectors) https://www.thingiverse.com/thing:6807372
  - Thingiverse 7170510 (jpfeng, 2025-10-13, R board, US 4×2 wall box + HLK-5M05 PSU, openHASP) https://www.thingiverse.com/thing:7170510
  - MakerWorld 1151020 (antonio.ciolino, 2025-02-25, **USB-C revision** cutouts) https://makerworld.com/en/models/1151020-cyd-3-5-case-esp32-3248s035-usb-c-remix
  - CAD model: https://grabcad.com/library/esp32-3248s035-cyd-1

---

## 8. LovyanGFX v1 device classes

Both compile against LovyanGFX 1.x (`#define LGFX_USE_V1`). Pins are common to R and C; only the touch block differs.

### 8a. ESP32-3248S035R (ST7796 + XPT2046) — from macsbug's tested config, factory demo cross-checked

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_3248S035R : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796   _panel;
  lgfx::Bus_SPI        _bus;
  lgfx::Light_PWM      _light;
  lgfx::Touch_XPT2046  _touch;
public:
  LGFX_3248S035R() {
    { auto c = _bus.config();
      c.spi_host    = SPI2_HOST;      // HSPI
      c.spi_mode    = 0;
      c.freq_write  = 40000000;       // macsbug 40 MHz; factory demo uses 80 MHz
      c.freq_read   = 16000000;
      c.spi_3wire   = false;
      c.use_lock    = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 14; c.pin_mosi = 13; c.pin_miso = 12; c.pin_dc = 2;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = 15; c.pin_rst = -1; c.pin_busy = -1;
      c.memory_width = 320; c.memory_height = 480;
      c.panel_width  = 320; c.panel_height  = 480;
      c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 0;
      c.dummy_read_pixel = 8; c.dummy_read_bits = 1;
      c.readable  = true;            // MISO is wired; set false if reads misbehave
      c.invert    = false;
      c.rgb_order = false;           // BGR
      c.dlen_16bit = false;
      c.bus_shared = true;           // touch shares the bus
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = 27; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config();
      c.x_min = 360; c.x_max = 4200;  // macsbug; factory demo: 222/3367
      c.y_min = 180; c.y_max = 3900;  //          factory demo: 192/3732
      c.pin_int = -1;                 // GPIO36 exists but is unreliable at 240 MHz -> poll
      c.bus_shared = true;
      c.offset_rotation = 3;          // macsbug (with setRotation 1/3); factory: 6 with rotation 0 [verify]
      c.spi_host = SPI2_HOST; c.freq = 1000000;
      c.pin_sclk = 14; c.pin_mosi = 13; c.pin_miso = 12; c.pin_cs = 33;
      _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};
// usage: LGFX_3248S035R tft; tft.init(); tft.setRotation(1); // landscape, USB right (3 = USB left)
// then tft.calibrateTouch(...) once and tft.setTouchCalibrate(saved) on boot.
```
Sources: https://macsbug.wordpress.com/2022/10/02/esp32-3248s035/ ; factory `LVGL_Arduino_3.5RTP_for_LovyanGFX.ino` https://github.com/lsdlsd88/ESP32-3248S035/tree/main/3.5inch_ESP32-3248S035/1-Demo/Demo_Arduino/7_3_LVGL_Arduino_3.5RTP_for_LovyanGFX ; rzeldent esp32-3248S035R.json.

### 8b. ESP32-3248S035C (ST7796 + GT911) — from LovyanGFX issue #811 / discussion #812 ("LGFX_Jingcai_ESP32-3248S035C.hpp", tested with the LVGL example)

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_3248S035C : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_GT911   _touch;
public:
  LGFX_3248S035C() {
    { auto c = _bus.config();
      c.spi_host   = VSPI_HOST;       // as posted in #811 (== SPI3_HOST); SPI2_HOST/HSPI works too (eupherion)
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.spi_3wire  = true;            // as posted; false also works
      c.use_lock   = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 14; c.pin_mosi = 13; c.pin_miso = 12; c.pin_dc = 2;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = 15; c.pin_rst = -1; c.pin_busy = -1;
      c.panel_width = 320; c.panel_height = 480;
      c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 0;
      c.dummy_read_pixel = 8; c.dummy_read_bits = 1;
      c.readable  = true;
      c.invert    = false;
      c.rgb_order = false;
      c.dlen_16bit = false;
      c.bus_shared = true;
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = 27; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config();
      c.x_min = 0; c.x_max = 319;     // factory demo trims to 14..310 / 5..448 [optional]
      c.y_min = 0; c.y_max = 479;
      c.pin_int = -1;                 // INT not wired to the ESP32 as shipped (R25 unpopulated) -> poll
      c.pin_rst = 25;
      c.bus_shared = false;
      c.offset_rotation = 0;
      c.i2c_port = 1;                 // factory demo uses port 0 at 800 kHz
      c.i2c_addr = 0x5D;              // 0x14 on some panels
      c.pin_sda = 33; c.pin_scl = 32;
      c.freq = 400000;
      _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};
// usage: LGFX_3248S035C tft; tft.init(); tft.setRotation(1); // landscape, USB right (eupherion uses 1)
```
Sources: https://github.com/lovyan03/LovyanGFX/issues/811 , https://github.com/lovyan03/LovyanGFX/discussions/812 , https://github.com/eupherion/ESP32-3248S035-LGFX-NTP-Clock (same values on SPI2_HOST), openHASP `esp32-3248s035.ini` (GT911 SDA33/SCL32/RST25/IRQ-1, 0x5D, port 1, 400 kHz), factory `3.5_LVGL_IOS_Capacitive_touch.ino`.

---

## Source index

- rzeldent board defs: https://github.com/rzeldent/platformio-espressif32-sunton (JSONs: esp32-3248S035R.json, esp32-3248S035C.json); driver lib https://github.com/rzeldent/esp32-smartdisplay
- openHASP: https://www.openhasp.com/0.7.0/hardware/sunton/esp32-3248s035/ ; config https://github.com/HASwitchPlate/openHASP/blob/master/user_setups/esp32/esp32-3248s035.ini ; discussion #384
- esp3d: https://esp3d.io/esp3d-tft/version_1x/hardware/esp32/sunton-35-3248/
- macsbug: https://macsbug.wordpress.com/2022/10/02/esp32-3248s035/ (PCB layout PDF https://macsbug.wordpress.com/wp-content/uploads/2022/09/esp32-3248s035r_pcb_layout-5.pdf ); S3 transplant https://macsbug.wordpress.com/2025/10/23/modified-esp32-3248s035r-to-esp32-s3/
- Factory package mirror (spec PDF, schematics ESP32-3248S035-MCU-V1.1.jpg / -LCM-V1.1.jpg, demos): https://github.com/lsdlsd88/ESP32-3248S035 ; original zip http://pan.jczn1688.com/directlink/1/ESP32%20module/3.5inch_ESP32-3248S035.zip
- LovyanGFX: issue #811, discussion #812; Panel_LCD.hpp (readCommand)
- ardnew BSP: https://github.com/ardnew/ESP32-3248S035 ; Type-C revision: https://github.com/chacuavip10/CYD-3.5inch_ESP32-3248S035
- Community: https://community.home-assistant.io/t/help-making-esp32-3248s035-work/748332 ; https://github.com/arendst/Tasmota/discussions/22224 ; https://github.com/Bodmer/TFT_eSPI/discussions/898 ; https://androidcrypto.github.io/ESP32-Boards/esp32_cyd_st7796_3_5_inches ; https://www.espboards.dev/esp32/cyd-esp32-3248s035/ ; https://homeding.github.io/boards/esp32/panel-3248S035.htm
- Manuals: https://manuals.plus/ae/1005008762308386 , https://manuals.plus/ae/1005008398513238 , https://manuals.plus/ae/1005008660264581 (seller's generic 2.8" manual)
- Random Nerd Tutorials has no 3.5" article; its CYD material covers the 2.8" ESP32-2432S028R only (https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/ ).
