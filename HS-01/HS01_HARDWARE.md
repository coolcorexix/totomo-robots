# HS-01 Hardware Context

> **Paste this whole file into your AI prompt** before asking it to write code for the
> HS-01 board. It tells the AI exactly which parts are on the board, which pins they use,
> and the mistakes that make sketches silently fail. The more of this the AI sees, the
> less it will guess wrong.

---

## What HS-01 is

HS-01 is our storyteller-box kit. Every sketch in this repo targets it.

- **MCU:** ESP32-S3 (Arduino FQBN `esp32:esp32:esp32s3`, generic Dev Module)
- **USB:** native USB CDC — shows up as `/dev/cu.usbmodem*` when flashing
- **Flash:** 16 MB (confirmed via `esptool flash-id` directly on hardware — do
  NOT assume 4MB just because most sketches here use a 4MB-sized partition
  table like `huge_app`; that's a choice, not the chip's real limit. A 16MB
  partition table with dual 6MB OTA slots — e.g. xiaozhi-esp32's
  `bread-compact-wifi` builds — fits fine.)
- **PSRAM:** 8 MB, **OPI (octal)** type
- **PlatformIO board:** `esp32-s3-devkitc-1`, `framework = arduino`

---

## Pinout (this is the source of truth)

| Peripheral            | Signal        | GPIO |
|-----------------------|---------------|------|
| **Display** (ST7789)  | SCLK          | 21   |
|                       | MOSI / SDA    | 47   |
|                       | DC            | 40   |
|                       | CS            | 41   |
|                       | BL (backlight)| 42   |
|                       | RST           | 45   |
| **Speaker** (I2S amp) | DIN           | 7    |
|                       | BCLK          | 15   |
|                       | LRC / WS      | 16   |
| **Microphone** (I2S)  | WS            | 4    |
|                       | SCK           | 5    |
|                       | SD            | 6    |
| **Button** (BOOT)     | —             | 0    |

Copy-paste block for a sketch:

```cpp
// ---- HS-01 pins ----
#define PIN_BTN     0

#define TFT_SCLK   21
#define TFT_MOSI   47
#define TFT_DC     40
#define TFT_CS     41
#define TFT_BL     42   // backlight — MUST be driven HIGH
#define TFT_RST    45

#define PIN_MIC_WS   4
#define PIN_MIC_SCK  5
#define PIN_MIC_SD   6

#define PIN_SPK_DIN    7
#define PIN_SPK_BCLK  15
#define PIN_SPK_LRC   16
```

---

## Display — ST7789 240×240 TFT over SPI

**It is a color SPI TFT, NOT an I2C OLED (SSD1306).** Do not use `Adafruit_SSD1306`
or I2C `Wire` code.

- Library: `Adafruit ST7735 and ST7789 Library` + `Adafruit GFX Library`
- Correct init sequence (order matters):

```cpp
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

void setupDisplay() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);                // 1. backlight ON or screen stays black
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS); // 2. remap SPI to HS-01 pins
  tft.init(240, 240, SPI_MODE3);             // 3. ST7789 init (NOT initR)
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);
}
```

**Common failures:**
- `tft.initR(...)` is for the **ST7735** and takes a *tab color*, not a background color.
  ST7789 uses `tft.init(240, 240, SPI_MODE3)`.
- The 3-arg constructor uses the **default** SPI bus. You **must** call
  `SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS)` or nothing reaches the display.
- Forgetting `digitalWrite(TFT_BL, HIGH)` → the code runs but the screen is black.

**Smooth animation:** don't `fillScreen()` every frame (it flickers). Draw the moving
object inside a bounding box, and each frame erase only the previous box
(`fillRect(prevX, prevY, w, h, BLACK)`) then redraw at the new position. See
`animal-tv/animal-tv.ino` for a working example (~30 fps).

---

## Speaker — I2S amplifier (MAX98357A or similar)

- Library: `Audio` (ESP32-audioI2S). **Add it to your project** if you use audio —
  it is not in every sketch's `lib_deps`.
- Pinout call order is **(BCLK, LRC, DIN)**:

```cpp
#include <Audio.h>
Audio audio;

audio.setPinout(PIN_SPK_BCLK, PIN_SPK_LRC, PIN_SPK_DIN); // (15, 16, 7)
audio.setVolume(15);                                     // 0..21
```

**Common failures:**
- `audio.loop()` and `audio.pauseResume()` do nothing unless you first give it a
  source: `audio.connecttohost("http://...")` or `audio.connecttoFS(...)`.
- Wrong pin numbers (e.g. the classic-ESP32 example pins 26/25/22) → silence.
- **This table was wrong for ~a whole session before being caught** (BCLK/DIN
  were swapped) — `setPinout()` was never even being *called* at first (silence,
  no error), and once it was added with the pins as originally listed here, the
  robot still stayed silent with zero errors logged (decode succeeded,
  `stream ready`, no I2S write errors — everything *looked* fine). The bug only
  surfaced by diffing against `storyteller/src/main.cpp` (a sketch verified
  working on real hardware), which uses DIN=7, BCLK=15, LRC=16 — the values now
  in the table above. **If a future sketch goes silent with a fully successful
  decode log, suspect this table before anything else — silence with no
  error is exactly what a pin swap looks like.**
- **ESP-IDF 5 / arduino-esp32 core 3.x will not let a sketch mix the legacy
  `driver/i2s.h` API and the new `driver/i2s_std.h` API** — it aborts at boot
  with `i2s(legacy): CONFLICT!`. `Audio` (ESP32-audioI2S ≥3.x) uses the new
  driver internally, so a custom mic-capture routine in the same sketch MUST
  also use `i2s_std.h` (`i2s_new_channel` / `i2s_channel_init_std_mode` /
  `i2s_channel_read`), not the old `i2s_driver_install` / `i2s_read`. See
  `i2sMicInit()` in the sibling `totomo-music` repo's
  `esp32/music-player/music-player.ino` for a working example.

---

## Microphone — I2S

Onboard I2S mic on WS=4, SCK=5, SD=6. Use the ESP-IDF/Arduino I2S driver in RX mode.
Sample rate we use for voice: **16000 Hz**.

---

## Button — BOOT on GPIO0

- Active **LOW**, wire it as `INPUT_PULLUP`:
  ```cpp
  pinMode(PIN_BTN, INPUT_PULLUP);   // pressed == LOW
  ```
- **Gotcha:** flashing/serial resets toggle GPIO0 and can look like a held press.
  On a menu screen, require a *fresh* press (wait for release first) before acting.

---

## Build / upload flags

### PlatformIO (`platformio.ini`)

```ini
[env:esp32s3]
platform  = espressif32
board     = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200

lib_deps =
    adafruit/Adafruit ST7735 and ST7789 Library @ ^1.10.0
    adafruit/Adafruit GFX Library @ ^1.11.0
    bblanchon/ArduinoJson @ ^7.0.0
    ; add the Audio (ESP32-audioI2S) lib here if the sketch plays sound
```

### arduino-cli

```bash
# compile (display-only or WiFi-only sketch)
arduino-cli compile --fqbn esp32:esp32:esp32s3 <sketch-folder>

# upload (find the port with: arduino-cli board list)
arduino-cli upload -p /dev/cu.usbmodem* --fqbn esp32:esp32:esp32s3 <sketch-folder>
```

**Anything with audio + WiFi/TLS** needs the extended FQBN:

```
esp32:esp32:esp32s3:PartitionScheme=huge_app,PSRAM=opi
```

- `PartitionScheme=huge_app` (3 MB app) — without it: *"text section exceeds available space"*.
- `PSRAM=opi` — the 8 MB PSRAM is **octal**. Using `PSRAM=enabled` (QSPI) is the wrong
  type and the audio lib reports *"PSRAM disabled"*.
- A WiFi-only / display-only sketch fits the default partition with no PSRAM.

---

## Checklist before asking AI for HS-01 code

1. Tell it: **ESP32-S3, ST7789 SPI TFT (not OLED), I2S speaker + mic, BOOT button on GPIO0.**
2. Give it the pin table above.
3. Remind it: **backlight HIGH**, **`SPI.begin` remap**, **`tft.init` (not `initR`)**,
   **audio needs a source**, **button is active-LOW pull-up**.
4. For audio sketches, tell it to use the **`huge_app` partition + `PSRAM=opi`**.
</content>
