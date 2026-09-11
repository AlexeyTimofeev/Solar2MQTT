# Alternative ESP32-S3 display boards for Solar2MQTT (LovyanGFX, Arduino-ESP32 3.x / pioarduino)

Research date: 2026-09-10. Web research only. Requirement: LovyanGFX v1, one free UART pin pair (any GPIO works as UART on ESP32-S3 via the GPIO matrix) plus optionally one GPIO for a DS18B20.

## TL;DR

| | A: Elecrow CrowPanel Advance 3.5 | B: Sunton ESP32-8048S043C |
|---|---|---|
| MCU / memory | ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM) | ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM) |
| Panel | 3.5" 480x320 IPS, ILI9488, 4-wire SPI @ 40 MHz | 4.3" 800x480 IPS, ST7262, 16-bit parallel RGB565 |
| Touch | GT911, I2C (SDA15/SCL16, INT47, RST48) | GT911, I2C (SDA19/SCL20, RST38, INT not wired) |
| Framebuffer | none needed (SPI panel) | 768 KB in PSRAM (mandatory) |
| Best UART connector | J15 "UART1-OUT" HY2.0-4P: RX IO18, TX IO17, 3V3, GND | P4 (1.25 mm 4-pin): IO18, IO17, 3V3, GND |
| DS18B20 GPIO | IO10 or IO9 on wireless header J9 (needs IO45=LOW), or IO1 on J9.1 | IO13/12/11 on P2 (SD bus, leave slot empty) or IO17 if UART goes on P1 |
| Native USB-CDC | yes (J2, GPIO19/20) + CH340K on J1 | no (19/20 used by touch); CH340C only |
| Battery | PH2.0 + TP4059 charger + boost | none |
| Case | acrylic case, +$1.80 on Elecrow | none official; 3D-printable designs |
| Price (2026) | $25.90 / $27.70 with case (Elecrow, list $37.90/$39.70) | $26.90 (Makerfabs), EUR 39.95 (HobbyElectronica), ~US$20-28 AliExpress (unconfirmed) |
| Main risk | GPIO2 shared by LCD RST and wireless-slot RST/CSN; few free pins | RGB panel + Wi-Fi flicker, PSRAM contention, LovyanGFX version sensitivity |

---

## Board A: Elecrow CrowPanel Advance 3.5-HMI ESP32 AI Display (480x320)

Primary sources: Elecrow wiki (https://www.elecrow.com/wiki/CrowPanel_Advance_3.5-HMI_ESP32_AI_Display.html), product page (https://www.elecrow.com/crowpanel-advance-3-5-hmi-esp32-ai-display-480x320-artificial-intelligent-ips-touch-screen.html), official repo https://github.com/Elecrow-RD/CrowPanel-Advance-3.5-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-480x320 (branch `master`: `example/V1.0/Arduino/lesson-03/3_5LVGL/LovyanGFX_Driver.h`, `Eagle_SCH&PCB/1.4/readme.md` hardware guide, `Eagle_SCH&PCB/1.4/ESP32-Display-3.5-inch-V1.4.pdf` schematic), Meshtastic variant https://github.com/meshtastic/firmware/tree/master/variants/esp32s3/elecrow_panel (`platformio.ini` env `elecrow-adv-35-tft`, `variant.h`).

### A1. Display

ILI9488 over SPI2_HOST, mode 0, 40 MHz write / 16 MHz read, DMA auto. Elecrow's own LovyanGFX driver and Meshtastic agree on every pin.

| Signal | GPIO | Note |
|---|---|---|
| SCLK | 42 | |
| MOSI | 39 | write-only |
| MISO | -1 | not wired (`readable=false`) |
| DC | 41 | |
| CS | 40 | |
| RST | 2 (code) | Elecrow guide: schematic shows an RC/diode network on TFT_RST, code drives GPIO2; GPIO2 is also wireless-slot NRESET/CSN (J11.1). Using `pin_rst=-1` is a safe alternative (panel resets via RC on power-up). Unconfirmed which is electrically true on your revision. |
| BL | 38 | active-high via SS8050 NPN; examples do `pinMode(38,OUTPUT); digitalWrite(38,HIGH)`; PWM dimming possible (guide: verify frequency) |
| Panel flags | | `invert=true`, `rgb_order=false`, `offset_rotation=3` (landscape 480x320), memory 320x480 |

ILI9488 in SPI mode is 18-bit color (3 bytes/pixel); LovyanGFX handles this transparently. Full 480x320 redraw ~460 KB ~ 92 ms at 40 MHz.

### A2. Touch

GT911 on I2C0: SDA 15, SCL 16, INT 47, RST 48, 400 kHz. Address: Elecrow code uses 0x14, wiki/Meshtastic say 0x5D (depends on INT level during reset). LovyanGFX `Touch_GT911` automatically toggles between 0x14 and 0x5D on init failure (`src/lgfx/v1/touch/Touch_GT911.cpp`, `default_addr_1/2`), so either value works. The I2C bus is shared with the on-board PCF8563/BM8563 RTC (0x51, CR1220 backup) and with the J13 "I2C-OUT" connector (4.7 k pull-ups on board). Touch range 0..319 x 0..479, `offset_rotation=0`.

### A3. Connectors and free GPIOs

From the Elecrow hardware guide (`Eagle_SCH&PCB/1.4/readme.md`) and wiki:

| Connector | Type | Pins | Notes |
|---|---|---|---|
| J15 UART1-OUT | HY2.0-4P (Grove) | 1=ESP RX IO18, 2=ESP TX IO17, 3=3V3, 4=GND | 3.3 V TTL, no other on-board use. Best UART link. |
| J13 I2C-OUT | HY2.0-4P (Grove) | 1=SCL16, 2=SDA15, 3=3V3, 4=GND | shared with GT911 + RTC |
| J10 UART0-IN | XH2.54-4P | 1=RXD0_H, 2=TXD0_H, 3=5V in, 4=GND | UART0 (IO44/IO43) behind BSS138 level network, shared with CH340K download/log; wiki rates connector 5 V +-5 %, 2 A. Data-pin level unconfirmed: guide warns to treat as 3.3 V. |
| J9 wireless (row 1) | 2x7 female header | 1=IO1 (via series R), 2=SCLK IO10, 3=MISO IO9, 4=MOSI IO3, 5=3V3, 6=GND, 7=5V (L3 NC, not guaranteed) | IO9/IO10 go through SGM3799 analog switch: IO45=LOW routes them here (mic off), IO45=HIGH routes to the PDM mic |
| J11 wireless (row 2) | 2x7 female header | 1=IO2 (via series R), 2=SCL16, 3=SDA15, 5=BUSY IO46, 6=CS IO0 | IO0=BOOT button, IO46 strapping |
| J3 BAT | PH2.0-2P | 3.7-4.2 V Li-ion | TP4059 charger, RY3420 boost; no battery ADC to ESP32 |
| J12 SPK | PH2.0-2P | NS4168 differential out | neither pin to GND |
| J5 TF | microSD | MISO 4, SCLK 5, MOSI 6, CS 7 (dedicated SPI) | 10 k pull-ups |
| J1 USB-C | | CH340K -> UART0 (IO43/44), 5 V in, auto-download | programming port |
| J2 USB-C | | native USB D-=IO19, D+=IO20 via 0 R options | population unverified |

Other fixed pins: I2S amp BCLK 13 / LRCLK 11 / DATA 12 / CTRL 21 (LOW = amp on, per verified code); mic CLK 9 / DATA 10 (muxed); buzzer 8; IO45 = mux select; IO14 = optional TFT power switch (NC parts); BOOT IO0; RESET EN. No user/RGB LED (only power/charge LEDs driven by an STC8G1K08). Strapping pins in play: IO0 (slot NSS), IO3 (slot MOSI), IO45 (mux), IO46 (slot BUSY). On this module GPIO26-32 are flash and 33-37 are octal PSRAM (not bonded out).

Recommended for Solar2MQTT:
- UART link on J15: `Serial1.begin(baud, SERIAL_8N1, /*rx*/18, /*tx*/17)`. Grove cable is included in the box.
- DS18B20: J9 pin 2 (IO10) or pin 3 (IO9), 3V3 = J9.5, GND = J9.6; set `pinMode(45,OUTPUT); digitalWrite(45,LOW)` once at boot (disables the mic). No wireless module may be fitted. Alternative with no mux dependency: J9.1 = IO1 (series resistor value unknown; unconfirmed for 1-Wire). Alternative with zero GPIO cost: a DS2482-100 1-Wire bridge on J13.
- Avoid IO2/IO0/IO46/IO3 for user I/O.

### A4. Power, case, price

- Power: 5 V/2 A via USB-C (J1 or J2) or the J10 5 V pin; Li-ion via J3 with charging. Board 101.4 x 63.3 x 15.8 mm, 120 g (150 g with case).
- Case: Elecrow product page has a "Case" configurable: "Without Acrylic Case" $25.90 (list $37.90) / "With Acrylic Case" $27.70 (list $39.70) (parsed from the product page option JSON, 2026-09). Wireless add-ons: SX1262 +$6.55, ESP32-H2 +$4.90, ESP32-C6 +$5.65, nRF24 +$3.50. Elecrow Meshtastic bundle (SX1262 + antenna): "as low as $36.30" (https://www.elecrow.com/crowpanel-advance-3-5-hmi-esp32-ai-display-for-meshtastic-320x240-ips-artificial-intelligent-screen.html).
- Resellers: ameridroid $41.95 (with case + SX1262) https://ameridroid.com/products/crowpanel-advance-3-5-esp32-s3-ai-hmi-display-for-meshtastic-480x320-ips-touch-screen-with-case; also RobotShop, OpenELAB, muzi.works, makerselectronics (EGP 2,850 with acrylic case). Amazon/AliExpress listings not verified.
- Box: board, USB-A to C cable, Grove 4-pin cable.

### A5. LovyanGFX class (ready to paste)

Based on Elecrow's `LovyanGFX_Driver.h` (URL above) plus `Light_PWM` on IO38 (backlight handled by `digitalWrite` in Elecrow's sketch; the PWM block is my addition, marked). Touch block is Elecrow's verified config.

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_CrowPanelAdv35 : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;   // added; Elecrow uses plain digitalWrite(38, HIGH)
  lgfx::Touch_GT911   _touch;
public:
  LGFX_CrowPanelAdv35() {
    { auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 42;
      cfg.pin_mosi = 39;
      cfg.pin_miso = -1;
      cfg.pin_dc   = 41;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    { auto cfg = _panel.config();
      cfg.pin_cs   = 40;
      cfg.pin_rst  = 2;      // Elecrow code; GPIO2 also = wireless-slot RST/CSN. Use -1 if you use the slot.
      cfg.pin_busy = -1;
      cfg.memory_width  = 320; cfg.memory_height = 480;
      cfg.panel_width   = 320; cfg.panel_height  = 480;
      cfg.offset_x = 0; cfg.offset_y = 0;
      cfg.offset_rotation = 3;   // landscape 480x320
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable   = false;
      cfg.invert     = true;
      cfg.rgb_order  = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;    // SD is on its own SPI bus (4/5/6/7); Elecrow sets true, harmless
      _panel.config(cfg);
    }
    { auto cfg = _light.config();   // added
      cfg.pin_bl = 38;
      cfg.invert = false;           // active high (verified: HIGH = on)
      cfg.freq   = 12000;           // unconfirmed; Elecrow does not PWM the backlight
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    { auto cfg = _touch.config();
      cfg.x_min = 0; cfg.x_max = 319;
      cfg.y_min = 0; cfg.y_max = 479;
      cfg.pin_int = 47;
      cfg.pin_rst = 48;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = I2C_NUM_0;
      cfg.pin_sda  = 15;
      cfg.pin_scl  = 16;
      cfg.freq     = 400000;
      cfg.i2c_addr = 0x14;   // 0x5D also seen; LovyanGFX retries the other address automatically
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
```

PlatformIO: `board = esp32-s3-devkitc-1`, `board_build.arduino.memory_type = qio_opi`, `board_build.flash_mode = qio`, `board_build.partitions = default_16MB.csv`, `build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` (or 0 if you log via CH340K on J1). PSRAM is not required for this SPI panel but is present.

### A6. Gotchas

- GPIO2 multi-use: LCD RST (code), wireless-slot NRESET / nRF24 CSN / Zigbee RX. Resetting the panel resets a fitted module and vice versa.
- IO45 selects mic (HIGH) vs wireless-SPI/J9 (LOW) through the SGM3799; stop the bus before switching. Product page says mic and SD cannot be used simultaneously; the schematic-based guide instead ties the mic to the wireless SPI pins. Either way the mic is irrelevant here.
- Hardware revisions: repo has schematics V1.0-V1.4 and the "V1.4" PDF's title block says V1.2; wiki notes "mute control IO21 from V1.2+". Confirm silkscreen.
- GT911 address 0x14 vs 0x5D: handled by LovyanGFX; other libraries need probing.
- UART0 (IO43/44) is shared between CH340K (J1) and J10; boot-ROM messages appear there. Prefer J15 (UART1) for the inverter link.
- J10 "5 V" data levels: the wiki calls the connector 5 V, the guide says treat all data lines as 3.3 V. Check with a meter before connecting the inverter's TTL.
- Battery charge state is not readable by the ESP32 (handled by a separate STC8G1K08).
- Meshtastic pins LovyanGFX 1.2.0 for the CrowPanel family ("v1.2.7 breaks the elecrow 7in display", an RGB board, not this one). The SPI ILI9488 path has no reported issues on 1.2.x; current release is 1.2.28 (2026-08-25).

---

## Board B: Sunton ESP32-8048S043C (4.3" 800x480 IPS, capacitive)

Primary sources: official LovyanGFX config https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx_user/LGFX_ESP32S3_RGB_ESP32-8048S043.h, ESPHome https://devices.esphome.io/devices/sunton-esp32-8048s043c/, rzeldent board definition https://github.com/rzeldent/platformio-espressif32-sunton (`esp32-8048S043C.json`), esp3d.io https://esp3d.io/esp3d-tft/version_1x/hardware/esp32-s3/sunton-43-8048/, espboards.dev https://www.espboards.dev/esp32/cyd-esp32-8048s043/, homeding https://homeding.github.io/boards/esp32s3/panel-8048S043.htm, macsbug layout PDF https://macsbug.wordpress.com/wp-content/uploads/2022/10/4280s043_layout-2.pdf, openHASP https://www.openhasp.com/0.7.0/hardware/sunton/esp32-8048s0xx/, Makerfabs wiki https://wiki.makerfabs.com/Sunton_ESP32_S3_4.3_inch_800x400_IPS_with_Touch.html (model SUTESPS343), LovyanGFX discussions #571 and #672.

### B1. Display

ST7262 800x480, 16-bit RGB565 parallel (5-6-5). All sources agree on the pin map (rzeldent labels the same GPIOs R/B swapped by naming only; LovyanGFX/ESPHome naming used here).

| Signal | GPIO |
|---|---|
| B0..B4 (d0..d4) | 8, 3, 46, 9, 1 |
| G0..G5 (d5..d10) | 5, 6, 7, 15, 16, 4 |
| R0..R4 (d11..d15) | 45, 48, 47, 21, 14 |
| DE (henable) | 40 |
| VSYNC | 41 |
| HSYNC | 39 |
| PCLK | 42 |
| Backlight | 2 (PWM, active high; ESPHome `ledc` on GPIO2, rzeldent `DISPLAY_BCKL=2`) |
| DISP | not connected |

Timings in the wild: LovyanGFX official 14 MHz, hsync fp/pw/bp = 8/4/16, vsync 4/4/4, polarities 0, `pclk_idle_high=1`; ESPHome 16 MHz, 8/4/8 and 8/4/8, `pclk_inverted: true`; rzeldent 12.5 MHz, pw 4 / bp 8 / fp 8 both, `PCLK_ACTIVE_NEG=true`. Framebuffer 800x480x2 = 768 KB must live in PSRAM (`use_psram=1`).

### B2. Touch

GT911 on I2C: SDA 19, SCL 20, RST 38, address 0x5D (0x14 alternate; LovyanGFX auto-toggles). INT is not connected to the ESP32 by default; esp3d.io: bridge R17 (0 R) to route INT to IO18 (and remove R5/U1 per that page). Polling only; LovyanGFX `pin_int` is used only for sleep/wake (discussion #571), so set `pin_int=-1`. The touch I2C bus is not shared with any other on-board device; IO19/IO20 are also brought out on P2/P3. Consequence: native USB (D-/D+ = 19/20) is unavailable, so no USB-CDC/JTAG; serial is via the CH340C on UART0.

### B3. Connectors and free GPIOs

Four 1.25 mm 4-pin JST-style headers plus microSD and USB-C (esp3d.io + espboards.dev, consistent with macsbug layout):

| Header | Pins | Notes |
|---|---|---|
| P1 "UART" | GND, RX (IO44/U0RXD), TX (IO43/U0TXD), 5 V | shared with CH340C USB-serial; 5 V pin can power the board |
| P2 | IO13 (SD MISO), IO12 (SD SCLK), IO11 (SD MOSI), IO19 (SDA) | SD lines free if no card is inserted (10 k pull-ups via RN1) |
| P3 | IO20 (SCL), IO19 (SDA), IO18, IO17 | no power pins |
| P4 | IO18, IO17, 3V3, GND | the two truly free GPIOs, with power |

Fixed: SD CS 10, MOSI 11, SCLK 12, MISO 13; BOOT button IO0; RESET = EN. No speaker/amplifier, no RGB LED, no light sensor, no battery connector on the 8048S043 (rzeldent feature table has those columns empty; macsbug layout shows only CH340C + two AMS1117 regulators). Strapping pins 3/45/46 are RGB data lines (fine; do not add external pull-downs). GPIO26-32 flash, 33-37 octal PSRAM.

Recommended for Solar2MQTT:
- Option 1 (cleanest, USB serial stays usable): UART link on P4: `Serial1.begin(baud, SERIAL_8N1, /*rx*/18, /*tx*/17)`, 3V3/GND on the same header. DS18B20 data on P2 IO13 (or IO12/IO11), leave the SD slot empty, add 4.7 k to 3V3 (there is already a 10 k on board), take 3V3/GND from P4.
- Option 2 (single header per device): UART on P1 (IO43 TX / IO44 RX, 5 V + GND available) and DS18B20 on P4 IO17 with 3V3/GND right there. Cost: UART0 is shared with the CH340C (its TX drives IO44 whenever USB is plugged; boot-ROM output at 115200 on IO43), so flash via USB, then disconnect, and disable serial logging.
- 1-Wire on IO18 conflicts only if you later bridge R17 for touch INT.

### B4. Power, case, price

- Power: 5 V via USB-C (CH340C port) or P1 5 V pin; Makerfabs: 4.75-5.25 V. No battery/charger.
- Case: none from Sunton. 3D-printable: https://www.printables.com/model/350540-sunton-esp32s3-8048s043c-43-screen-case, https://makerworld.com/en/models/1080017-sunton-esp32-8048s043-case, https://makerworld.com/en/models/1027067-sunton-cyd-esp32-8048s043-enclosure. Some AliExpress listings offer an acrylic shell option (unconfirmed).
- Price (2026-09): Makerfabs $26.90 (https://www.makerfabs.com/sunton-esp32-s3-4-3-inch-ips-with-touch.html); HobbyElectronica EUR 39.95 (https://www.hobbyelectronica.nl/en/product/4-3-ips-capacitive-touchscreen-esp32-8048s043c-i/); Amazon DIYmalls "ESP32-8048S043C_I" B0CLGCMWQ7 (price not retrievable, typically ~US$30-35, unconfirmed); AliExpress jczn1688 store items 1005006110360174 / 1005006112914604 (typically ~US$20-28, unconfirmed). Buy the "C" (capacitive) and "_I"/IPS 800x480 variant; the sibling ESP32-4827S043 (480x272 TN) uses the same PCB.

### B5. LovyanGFX class (ready to paste)

Verbatim from the official `LGFX_ESP32S3_RGB_ESP32-8048S043.h` (URL above) with two marked edits: `pin_int=-1` (INT not wired) and `i2c_addr=0x5D` (default on this board; auto-fallback covers 0x14).

```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>

class LGFX_Sunton8048S043 : public lgfx::LGFX_Device {
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;
  lgfx::Light_PWM   _light_instance;
  lgfx::Touch_GT911 _touch_instance;

  LGFX_Sunton8048S043(void) {
    { auto cfg = _panel_instance.config();
      cfg.memory_width  = 800; cfg.memory_height = 480;
      cfg.panel_width   = 800; cfg.panel_height  = 480;
      cfg.offset_x = 0; cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    { auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;            // framebuffer in PSRAM (required)
      _panel_instance.config_detail(cfg);
    }
    { auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0  = GPIO_NUM_8;  // B0
      cfg.pin_d1  = GPIO_NUM_3;  // B1
      cfg.pin_d2  = GPIO_NUM_46; // B2
      cfg.pin_d3  = GPIO_NUM_9;  // B3
      cfg.pin_d4  = GPIO_NUM_1;  // B4
      cfg.pin_d5  = GPIO_NUM_5;  // G0
      cfg.pin_d6  = GPIO_NUM_6;  // G1
      cfg.pin_d7  = GPIO_NUM_7;  // G2
      cfg.pin_d8  = GPIO_NUM_15; // G3
      cfg.pin_d9  = GPIO_NUM_16; // G4
      cfg.pin_d10 = GPIO_NUM_4;  // G5
      cfg.pin_d11 = GPIO_NUM_45; // R0
      cfg.pin_d12 = GPIO_NUM_48; // R1
      cfg.pin_d13 = GPIO_NUM_47; // R2
      cfg.pin_d14 = GPIO_NUM_21; // R3
      cfg.pin_d15 = GPIO_NUM_14; // R4
      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync   = GPIO_NUM_41;
      cfg.pin_hsync   = GPIO_NUM_39;
      cfg.pin_pclk    = GPIO_NUM_42;
      cfg.freq_write  = 14000000;   // lower to 12 MHz if you see flicker with Wi-Fi active
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch  = 16;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 4;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch  = 4;
      cfg.pclk_idle_high    = 1;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    { auto cfg = _light_instance.config();
      cfg.pin_bl = GPIO_NUM_2;      // active high
      _light_instance.config(cfg);
    }
    _panel_instance.light(&_light_instance);

    { auto cfg = _touch_instance.config();
      cfg.x_min = 0; cfg.x_max = 800;
      cfg.y_min = 0; cfg.y_max = 480;
      cfg.pin_int    = -1;          // edited: INT not wired (official file says GPIO_NUM_18, needs R17 bridge)
      cfg.pin_rst    = GPIO_NUM_38; // added (rzeldent/homeding); official file omits it
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port   = I2C_NUM_1;
      cfg.pin_sda    = GPIO_NUM_19;
      cfg.pin_scl    = GPIO_NUM_20;
      cfg.freq       = 400000;
      cfg.i2c_addr   = 0x5D;        // edited from 0x14; LovyanGFX retries the other address
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    setPanel(&_panel_instance);
  }
};
```

PlatformIO (from rzeldent's board JSON): `board_build.arduino.memory_type = qio_opi`, `board_build.flash_mode = qio`, `board_build.partitions = default_16MB.csv`, `build_flags = -DBOARD_HAS_PSRAM -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0` (logs on UART0/CH340C), 240 MHz, flash 80 MHz. Optionally `board = esp32-8048S043C` with `platform_packages`/`boards_dir` from rzeldent's repo.

### B6. Gotchas

- PSRAM framebuffer is mandatory; without `qio_opi` + `BOARD_HAS_PSRAM` `init()` crashes or fails (see LovyanGFX issue #735 for the symptom on a similar board).
- LovyanGFX version vs Arduino-ESP32 core: 1.2.7 does not compile `Bus_RGB.cpp` on core 3.3.x / IDF 5.5 (`gpio_hal_iomux_func_sel`, issue #739). Fixed by commits ac9198df + 80788f07 (2025-08-23); verified present in tags 1.2.19 (2026-01-23) through 1.2.28 (2026-08-25). Use 1.2.28 with pioarduino 3.3.x. Meshtastic still pins 1.2.0 because 1.2.7 broke the Elecrow 7" RGB panel, so test whichever version you pick on real hardware.
- Wi-Fi + RGB flicker/frame shift: heavy PSRAM traffic (Wi-Fi connect, LVGL buffers in PSRAM) starves the RGB DMA and shifts/flickers frames (LVGL forum threads for 8048S043; ESP-IDF RGB LCD docs). Mitigations: pclk 12-14 MHz, keep LVGL draw buffers in internal RAM (partial buffers), avoid full-screen redraws during Wi-Fi bursts; `CONFIG_LCD_RGB_RESTART_IN_VSYNC` / `SPIRAM_FETCH_INSTRUCTIONS` / `SPIRAM_RODATA` need custom IDF libs (not available with stock Arduino prebuilt libs).
- Colors: if blue shows as green under LVGL, set `LV_COLOR_16_SWAP 0` (discussion #672).
- Touch INT not wired; polling only. Bridging R17 puts INT on IO18 and takes away one of your two free GPIOs.
- No native USB (19/20 = touch I2C): no USB-CDC console, no USB-JTAG. Serial via CH340C; keep `ARDUINO_USB_CDC_ON_BOOT=0`.
- Very few free pins: only IO17/IO18 are unconditionally free; IO11/12/13 only while the SD slot is empty; IO43/44 only if you give up USB serial.
- Variant confusion: same PCB as ESP32-4827S043 (480x272 TN) and there are C/R/N touch variants; board revisions differ between production runs (openHASP/atomic14 note revision-dependent init on the 8048 family). Check the sticker/silkscreen.
- Backlight GPIO2 is active high; some boards ship with the backlight full-on until PWM is configured.
- No battery, no speaker, no LED, no RTC on this board.

---

## Recommendation for this project

- If you want the smallest change and a battery/case: Board A. UART on J15 (IO17/IO18, Grove cable included), DS18B20 on J9 IO10 with IO45 held LOW. Watch GPIO2 (LCD RST vs slot) and keep the wireless slot empty.
- If you want the big 800x480 panel: Board B. UART on P4 (IO17/IO18), DS18B20 on P2 IO13 with the SD slot empty. Budget for RGB/PSRAM/Wi-Fi tuning and use LovyanGFX 1.2.19+ with pioarduino 3.3.x. No case, no battery.

## Source list

- Elecrow wiki: https://www.elecrow.com/wiki/CrowPanel_Advance_3.5-HMI_ESP32_AI_Display.html
- Elecrow product page (prices, case option): https://www.elecrow.com/crowpanel-advance-3-5-hmi-esp32-ai-display-480x320-artificial-intelligent-ips-touch-screen.html
- Elecrow Meshtastic bundle: https://www.elecrow.com/crowpanel-advance-3-5-hmi-esp32-ai-display-for-meshtastic-320x240-ips-artificial-intelligent-screen.html
- Elecrow repo (LovyanGFX driver, hardware guide, schematics): https://github.com/Elecrow-RD/CrowPanel-Advance-3.5-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-480x320
- Elecrow wireless modules: https://www.elecrow.com/wireless-module-for-crowpanel-advanced-series.html
- Meshtastic variant: https://github.com/meshtastic/firmware/tree/master/variants/esp32s3/elecrow_panel and https://meshtastic.org/docs/hardware/devices/elecrow/crowpanel/
- ameridroid: https://ameridroid.com/products/crowpanel-advance-3-5-esp32-s3-ai-hmi-display-for-meshtastic-480x320-ips-touch-screen-with-case
- CNX Software overview: https://www.cnx-software.com/2025/01/17/crowpanel-advance-esp32-s3-displays-with-replaceable-wifi-6-thread-zigbee-lora-and-2-4ghz-wireless-modules/
- LovyanGFX official Sunton config: https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx_user/LGFX_ESP32S3_RGB_ESP32-8048S043.h
- LovyanGFX GT911 driver (address fallback): https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1/touch/Touch_GT911.cpp
- LovyanGFX issues/discussions: #571 https://github.com/lovyan03/LovyanGFX/discussions/571, #672 https://github.com/lovyan03/LovyanGFX/discussions/672, #739 https://github.com/lovyan03/LovyanGFX/issues/739, #735 https://github.com/lovyan03/LovyanGFX/issues/735
- ESPHome: https://devices.esphome.io/devices/sunton-esp32-8048s043c/
- rzeldent board defs: https://github.com/rzeldent/platformio-espressif32-sunton
- esp3d.io: https://esp3d.io/esp3d-tft/version_1x/hardware/esp32-s3/sunton-43-8048/
- espboards.dev: https://www.espboards.dev/esp32/cyd-esp32-8048s043/
- homeding: https://homeding.github.io/boards/esp32s3/panel-8048S043.htm
- macsbug layout: https://macsbug.wordpress.com/wp-content/uploads/2022/10/4280s043_layout-2.pdf
- NuttX board page: https://nuttx.apache.org/docs/latest/platforms/xtensa/esp32s3/boards/esp32s3-8048S043/index.html
- openHASP: https://www.openhasp.com/0.7.0/hardware/sunton/esp32-8048s0xx/
- Makerfabs: https://wiki.makerfabs.com/Sunton_ESP32_S3_4.3_inch_800x400_IPS_with_Touch.html, https://www.makerfabs.com/sunton-esp32-s3-4-3-inch-ips-with-touch.html
- HobbyElectronica: https://www.hobbyelectronica.nl/en/product/4-3-ips-capacitive-touchscreen-esp32-8048s043c-i/
- Amazon DIYmalls: https://www.amazon.com/DIYmalls-ESP32-8048S043C-I-Capacitive-ESP32-S3-WROOM-1-Development/dp/B0CLGCMWQ7
- LVGL forum flicker threads: https://forum.lvgl.io/t/display-glitching-while-connecting-to-wifi-esp32s3-8048s043/12075
- Cases: https://www.printables.com/model/350540-sunton-esp32s3-8048s043c-43-screen-case, https://makerworld.com/en/models/1080017-sunton-esp32-8048s043-case
