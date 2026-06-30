/*
 * WiFi Multi-Config Picker — Totomo Storyteller Box
 * ─────────────────────────────────────────────────
 * On boot the device tries the last-used WiFi automatically.
 * If it fails, a picker appears on the TFT so you can choose
 * another network without reflashing.
 *
 * Controls (single button):
 *   Short press  (<1 s)  → cycle to the next network in the list
 *   Long press   (≥1 s)  → connect to the highlighted network
 *   Any press on error   → go back to picker
 *
 * Hardware (matches the storyteller box schematic):
 *   • ESP32-S3
 *   • ST7789 240×240 TFT, SPI (HSPI)
 *       SCLK=21  MOSI=47  DC=40  CS=41  RST=45  BL=42 (backlight)
 *   • Boot button on GPIO0 (active LOW, built-in pull-up)
 *
 * Required libraries (install via Arduino Library Manager):
 *   • Adafruit ST7735 and ST7789 Library
 *   • Adafruit GFX Library
 *
 * ── EDIT SECTION ──────────────────────────────────────────────
 * Only change WIFI_CONFIGS[] and the pin/display constants below.
 * Everything else is self-contained.
 */

#include <WiFi.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Audio.h"

// ─── Pin & display ────────────────────────────────────────────────────────────
#define PIN_BTN      0       // Boot button (active LOW)

#define TFT_SCLK    21
#define TFT_MOSI    47
#define TFT_DC      40
#define TFT_CS      41
#define TFT_BL      42       // Backlight — MUST be driven HIGH or screen stays dark
#define TFT_RST     45

#define SCREEN_W   240
#define SCREEN_H   240

// Navy-blue background (RGB565). Used for all screen backgrounds.
#define BG_COLOR   0x0014    // ~#000080 navy blue

// ─── I2S speaker ──────────────────────────────────────────────────────────────
#define I2S_SPK_DIN    7
#define I2S_SPK_BCLK  15
#define I2S_SPK_LRC   16

// ─── Content sources (GitHub) ─────────────────────────────────────────────────
// Each mode points to a GitHub repo of .mp3 files served via GitHub Pages.
// Only the owner/repo part of the two URLs differs between entries.
struct ContentSource {
    const char* name;
    const char* apiUrl;
    const char* pagesBase;
};

const ContentSource SOURCES[] = {
    { "Stories", "https://api.github.com/repos/harshalshewale/stories/contents/",
                 "https://harshalshewale.github.io/stories/" },
    { "Songs",   "https://api.github.com/repos/coolcorexix/songs/contents/",
                 "https://coolcorexix.github.io/songs/" },
};
const int SOURCE_COUNT = sizeof(SOURCES) / sizeof(SOURCES[0]);

#define MAX_STORIES 20

// ─── Button timing ────────────────────────────────────────────────────────────
#define BTN_DEBOUNCE_MS   50
#define BTN_LONG_MS     1000

// ─── WiFi configs ─────────────────────────────────────────────────────────────
// Add or remove entries freely. Name is what shows on screen (ASCII only).
struct WifiConfig {
    const char* name;
    const char* ssid;
    const char* password;
};

const WifiConfig WIFI_CONFIGS[] = {
    { "To To Mo",        "To To Mo",   "biethoidehoc"     },
    { "CT1-1009",  "Nha Meo Va Phat",   "12345678"   },
};
const int WIFI_COUNT = sizeof(WIFI_CONFIGS) / sizeof(WIFI_CONFIGS[0]);

// ─── Globals ──────────────────────────────────────────────────────────────────
SPIClass         tftSPI(HSPI);
Adafruit_ST7789  tft = Adafruit_ST7789(&tftSPI, TFT_CS, TFT_DC, TFT_RST);
Preferences      prefs;
Audio            audio;

enum AppScreen { SCR_CONNECTING, SCR_PICKER, SCR_CONNECTED, SCR_ERROR, SCR_CATEGORY, SCR_PLAYER };
AppScreen screen    = SCR_CONNECTING;
int       selIdx    = 0;
int       sourceIdx = 0;   // selected content source (0=Stories, 1=Songs)
int       catSel    = 0;   // highlighted item in the category menu

// Button tracking (WiFi picker: short/long press)
static bool         btnWasPressed = false;
static unsigned long pressStartMs = 0;
static bool         longFired     = false;

// Playlist + player state
String  playlist[MAX_STORIES];
String  playlistNames[MAX_STORIES];
int     totalStories    = 0;
int     currentStory    = 0;
bool    isPlaying       = false;
bool    audioWasRunning = false;

// Player button tracking (multi-click: 1x/2x/3x, plus hold = back to menu)
static unsigned long lastPressMs     = 0;
static unsigned long lastReleaseMs   = 0;
static int           clickCount      = 0;
static bool          btnDown         = false;
static bool          playerLongFired = false;

// ─── Button reader ────────────────────────────────────────────────────────────
// Returns: 0=nothing  1=short press  2=long press
int readButton() {
    bool pressed = (digitalRead(PIN_BTN) == LOW);
    int  event   = 0;

    if (pressed && !btnWasPressed) {
        pressStartMs = millis();
        longFired    = false;
    }

    if (pressed && !longFired && (millis() - pressStartMs >= BTN_LONG_MS)) {
        longFired = true;
        event     = 2;
    }

    if (!pressed && btnWasPressed && !longFired) {
        if (millis() - pressStartMs >= BTN_DEBOUNCE_MS) {
            event = 1;
        }
    }

    btnWasPressed = pressed;
    return event;
}

// ─── Screen drawings ─────────────────────────────────────────────────────────
void drawConnecting(const char* ssid, int dots) {
    tft.fillScreen(BG_COLOR);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(10, 18);
    tft.println("Connecting");
    tft.drawLine(0, 55, SCREEN_W, 55, ST77XX_WHITE);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 75);
    tft.println("SSID:");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 100);
    tft.println(ssid);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 160);
    for (int i = 0; i <= dots % 4; i++) tft.print(".");
}

void drawPicker(int highlight) {
    tft.fillScreen(BG_COLOR);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(10, 12);
    tft.println("Select WiFi:");
    tft.drawLine(0, 40, SCREEN_W, 40, ST77XX_WHITE);

    // Show up to 4 items
    const int rowH   = 42;
    const int startY = 50;
    int firstVisible = max(0, min(highlight, WIFI_COUNT - 4));
    for (int i = firstVisible; i < min(firstVisible + 4, WIFI_COUNT); i++) {
        int row = i - firstVisible;
        int y   = startY + row * rowH;

        uint16_t nameColor;
        if (i == highlight) {
            tft.fillRect(0, y, SCREEN_W, rowH - 4, ST77XX_WHITE);
            nameColor = ST77XX_BLACK;
        } else {
            nameColor = ST77XX_WHITE;
        }

        tft.setTextSize(2);
        tft.setTextColor(nameColor);
        tft.setCursor(8, y + 4);
        tft.print(i == highlight ? "> " : "  ");
        tft.print(WIFI_CONFIGS[i].name);

        // SSID hint, smaller, underneath the name
        tft.setTextSize(1);
        tft.setTextColor(i == highlight ? ST77XX_BLACK : ST77XX_CYAN);
        tft.setCursor(28, y + 24);
        tft.print(WIFI_CONFIGS[i].ssid);
    }

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(8, 226);
    tft.print("Hold to connect");
}

void drawConnected(const char* name, const char* ip) {
    tft.fillScreen(BG_COLOR);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(10, 18);
    tft.println("Connected!");
    tft.drawLine(0, 55, SCREEN_W, 55, ST77XX_WHITE);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 80);
    tft.print("WiFi: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.println(name);

    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 120);
    tft.println("IP:");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 145);
    tft.println(ip);
}

void drawError(const char* name) {
    tft.fillScreen(BG_COLOR);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(10, 18);
    tft.println("Failed!");
    tft.drawLine(0, 55, SCREEN_W, 55, ST77XX_WHITE);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 80);
    tft.print("WiFi: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.println(name);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_ORANGE);
    tft.setCursor(10, 160);
    tft.println("Press to retry");
}

// ─── WiFi connect ─────────────────────────────────────────────────────────────
bool tryConnect(int idx, unsigned long timeoutMs = 12000) {
    const WifiConfig& cfg = WIFI_CONFIGS[idx];
    Serial.printf("[WIFI] Connecting to \"%s\" (%s)\n", cfg.name, cfg.ssid);

    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid, cfg.password);

    unsigned long start = millis();
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.println("[WIFI] Timeout");
            return false;
        }
        drawConnecting(cfg.ssid, dots++);
        delay(250);
    }
    return true;
}

// ─── Storyteller: player screen ───────────────────────────────────────────────
void drawPlayer(const String& title, const String& status, const String& instruction) {
    tft.fillScreen(BG_COLOR);

    tft.setTextSize(3);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(10, 14);
    tft.println(sourceIdx == 1 ? "MUSIC BOX" : "STORY BOX");
    tft.drawLine(0, 50, SCREEN_W, 50, ST77XX_WHITE);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(10, 64);
    tft.print("Status: ");
    tft.setTextColor(ST77XX_WHITE);
    tft.println(status);

    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(10, 100);
    tft.println(sourceIdx == 1 ? "Song:" : "Story:");
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 124);
    String t = title;
    if (t.length() > 18) t = t.substring(0, 15) + "...";
    tft.println(t);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_ORANGE);
    tft.setCursor(10, 200);
    tft.println(instruction);
}

// ─── Storyteller: fetch playlist from GitHub ──────────────────────────────────
void fetchPlaylist() {
    HTTPClient http;
    http.begin(SOURCES[sourceIdx].apiUrl);
    http.addHeader("User-Agent", "ESP32-S3");

    int code = http.GET();
    Serial.printf("[STORY] %s GitHub API HTTP %d\n", SOURCES[sourceIdx].name, code);
    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            totalStories = 0;
            for (JsonObject v : doc.as<JsonArray>()) {
                String n = v["name"].as<String>();
                if (n.endsWith(".mp3") && totalStories < MAX_STORIES) {
                    playlist[totalStories]      = String(SOURCES[sourceIdx].pagesBase) + n;
                    playlistNames[totalStories] = n;
                    totalStories++;
                }
            }
            Serial.printf("[STORY] Loaded %d stories\n", totalStories);
        } else {
            Serial.printf("[STORY] JSON parse error: %s\n", err.c_str());
        }
    }
    http.end();
}

// ─── Storyteller: play current story ──────────────────────────────────────────
void playStory() {
    if (totalStories == 0) {
        Serial.println("[STORY] No stories loaded");
        drawPlayer("No stories", "Load failed", "1x: Retry");
        return;
    }
    Serial.printf("[STORY] Playing: %s\n", playlist[currentStory].c_str());
    audio.connecttohost(playlist[currentStory].c_str());
    isPlaying       = true;
    audioWasRunning = false;
    drawPlayer(playlistNames[currentStory], "Playing", "1x:P/P 2x:Next 3x:Prev");
}

// ─── Category menu: Stories / Songs ───────────────────────────────────────────
void drawCategory(int highlight) {
    tft.fillScreen(BG_COLOR);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(10, 12);
    tft.println("Choose mode:");
    tft.drawLine(0, 40, SCREEN_W, 40, ST77XX_WHITE);

    const int rowH   = 48;
    const int startY = 60;
    for (int i = 0; i < SOURCE_COUNT; i++) {
        int y = startY + i * rowH;
        uint16_t nameColor;
        if (i == highlight) {
            tft.fillRect(0, y, SCREEN_W, rowH - 6, ST77XX_WHITE);
            nameColor = ST77XX_BLACK;
        } else {
            nameColor = ST77XX_WHITE;
        }
        tft.setTextSize(3);
        tft.setTextColor(nameColor);
        tft.setCursor(14, y + 6);
        tft.print(i == highlight ? "> " : "  ");
        tft.print(SOURCES[i].name);
    }

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(8, 226);
    tft.print("Tap: next   Hold: select");
}

// ─── Enter the category menu, ignoring any button still held from before ──────
void enterCategory() {
    screen = SCR_CATEGORY;
    catSel = 0;
    // Require a fresh press: ignore a button (or serial DTR line) still held low
    btnWasPressed = true;
    longFired     = true;
    drawCategory(catSel);
}

// ─── Init audio + load the selected source, then enter the player ─────────────
void startPlayer() {
    audio.setPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN);
    audio.setVolume(21);
    audio.forceMono(true);

    currentStory    = 0;
    isPlaying       = false;
    audioWasRunning = false;
    clickCount      = 0;
    // Swallow the select-hold so the player button ignores this in-progress press
    btnDown         = true;
    playerLongFired = true;
    lastPressMs     = millis();

    drawPlayer("...", "Loading", "Please wait");
    fetchPlaylist();

    screen = SCR_PLAYER;
    if (totalStories > 0) {
        drawPlayer(playlistNames[currentStory], "Ready", "1x:Play 2x:Next 3x:Prev");
    } else {
        drawPlayer("No items", "Load failed", "1x: Retry");
    }
}

// ─── Stop playback and return to the category menu ────────────────────────────
void backToCategory() {
    audio.stopSong();
    isPlaying       = false;
    audioWasRunning = false;
    clickCount      = 0;
    // Swallow the in-progress hold so readButton() in the menu ignores it
    btnWasPressed = true;
    longFired     = true;
    screen = SCR_CATEGORY;
    catSel = sourceIdx;
    drawCategory(catSel);
}

// ─── Player button: tap 1x play/pause, 2x next, 3x prev, hold = back to menu ───
void handlePlayerButton() {
    unsigned long now = millis();
    bool pressed = (digitalRead(PIN_BTN) == LOW);

    // Press edge
    if (pressed && !btnDown) {
        btnDown         = true;
        lastPressMs     = now;
        playerLongFired = false;
    }

    // Hold ≥ BTN_LONG_MS → back to category menu
    if (pressed && btnDown && !playerLongFired && (now - lastPressMs >= BTN_LONG_MS)) {
        playerLongFired = true;
        backToCategory();
        return;
    }

    // Release edge → register a click (unless this was a long press)
    if (!pressed && btnDown) {
        btnDown = false;
        if (!playerLongFired && (now - lastPressMs >= BTN_DEBOUNCE_MS)) {
            lastReleaseMs = now;
            clickCount++;
        }
    }

    // Resolve the click count once the multi-click gap has elapsed
    if (clickCount > 0 && !btnDown && (now - lastReleaseMs > 450)) {
        if (clickCount == 1) {
            if (totalStories == 0) {
                startPlayer();              // retry loading this source
                return;
            } else if (!isPlaying && !audio.isRunning()) {
                playStory();
            } else {
                audio.pauseResume();
                isPlaying = audio.isRunning();
                drawPlayer(playlistNames[currentStory],
                           isPlaying ? "Playing" : "Paused",
                           "1x:P/P 2x:Next 3x:Prev");
            }
        } else if (clickCount == 2 && totalStories > 0) {
            currentStory = (currentStory + 1) % totalStories;
            playStory();
        } else if (clickCount == 3 && totalStories > 0) {
            currentStory = (currentStory - 1 + totalStories) % totalStories;
            playStory();
        }
        clickCount = 0;
    }
}

// ─── setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Totomo WiFi Picker ===");

    pinMode(PIN_BTN, INPUT_PULLUP);

    // Backlight ON first — without this the screen stays dark
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
    tft.init(SCREEN_W, SCREEN_H, SPI_MODE3);
    tft.setRotation(0);   // flipped 180° (upside down) vs. default
    tft.fillScreen(BG_COLOR);

    // Restore last-used index from flash
    prefs.begin("wifi", true);   // read-only
    selIdx = prefs.getInt("lastIdx", 0);
    prefs.end();
    if (selIdx < 0 || selIdx >= WIFI_COUNT) selIdx = 0;

    Serial.printf("[WIFI] Auto-trying last config: %s\n", WIFI_CONFIGS[selIdx].ssid);
    if (tryConnect(selIdx, 8000)) {
        // Save (in case order changed since last boot)
        prefs.begin("wifi", false);
        prefs.putInt("lastIdx", selIdx);
        prefs.end();

        String ip = WiFi.localIP().toString();
        Serial.printf("[WIFI] Connected! IP=%s\n", ip.c_str());
        drawConnected(WIFI_CONFIGS[selIdx].name, ip.c_str());
        delay(1500);

        // ── Show the content-mode menu (Stories / Songs) ──────────────────
        enterCategory();
        return;
    }

    Serial.println("[WIFI] Auto-connect failed — showing picker");
    screen = SCR_PICKER;
    drawPicker(selIdx);
}

// ─── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    // ── Storyteller running — audio + playback controls ──────────────────────
    if (screen == SCR_PLAYER) {
        audio.loop();

        // Auto-advance when a story finishes playing
        bool running = audio.isRunning();
        if (isPlaying && audioWasRunning && !running && totalStories > 0) {
            currentStory = (currentStory + 1) % totalStories;
            playStory();
        }
        audioWasRunning = running;

        handlePlayerButton();
        return;
    }

    int btn = readButton();

    if (screen == SCR_PICKER) {
        if (btn == 1) {
            // Short press: advance selection
            selIdx = (selIdx + 1) % WIFI_COUNT;
            Serial.printf("[BTN] Highlighted: %s\n", WIFI_CONFIGS[selIdx].name);
            drawPicker(selIdx);

        } else if (btn == 2) {
            // Long press: connect to highlighted config
            Serial.printf("[BTN] Connecting to: %s\n", WIFI_CONFIGS[selIdx].ssid);
            screen = SCR_CONNECTING;

            if (tryConnect(selIdx, 15000)) {
                prefs.begin("wifi", false);
                prefs.putInt("lastIdx", selIdx);
                prefs.end();

                String ip = WiFi.localIP().toString();
                Serial.printf("[WIFI] Connected! IP=%s\n", ip.c_str());
                drawConnected(WIFI_CONFIGS[selIdx].name, ip.c_str());
                delay(1500);

                // ── Show the content-mode menu (Stories / Songs) ──────────
                enterCategory();
            } else {
                Serial.printf("[WIFI] Failed to connect to %s\n", WIFI_CONFIGS[selIdx].ssid);
                screen = SCR_ERROR;
                drawError(WIFI_CONFIGS[selIdx].name);
            }
        }

    } else if (screen == SCR_CATEGORY) {
        if (btn == 1) {
            // Short press: highlight next mode
            catSel = (catSel + 1) % SOURCE_COUNT;
            drawCategory(catSel);
        } else if (btn == 2) {
            // Long press: select this mode and load it
            sourceIdx = catSel;
            Serial.printf("[MODE] Selected: %s\n", SOURCES[sourceIdx].name);
            startPlayer();
        }

    } else if (screen == SCR_ERROR) {
        if (btn != 0) {
            // Any button press → back to picker
            screen = SCR_PICKER;
            drawPicker(selIdx);
        }
    }
}
