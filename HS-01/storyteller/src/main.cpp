#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Audio.h>
#include "hs01_wifi.h"

#define PIN_BTN      0

#define TFT_SCLK    21
#define TFT_MOSI    47
#define TFT_DC      40
#define TFT_CS      41
#define TFT_BL      42
#define TFT_RST     45

#define SCREEN_W   240
#define SCREEN_H   240

#define BG_COLOR   0x0014

#define I2S_SPK_DIN    7
#define I2S_SPK_BCLK  15
#define I2S_SPK_LRC   16

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

#define BTN_DEBOUNCE_MS   50
#define BTN_LONG_MS     1000

const hs01::WifiConfig WIFI_CONFIGS[] = {
    { "To To Mo",  "To To Mo",         "biethoidehoc" },
    { "CT1-1009",  "Nha Meo Va Phat",  "12345678"     },
};
const int WIFI_COUNT = sizeof(WIFI_CONFIGS) / sizeof(WIFI_CONFIGS[0]);

SPIClass         tftSPI(HSPI);
Adafruit_ST7789  tft = Adafruit_ST7789(&tftSPI, TFT_CS, TFT_DC, TFT_RST);
Audio            audio;

enum AppScreen { SCR_CATEGORY, SCR_PLAYER };
AppScreen screen    = SCR_CATEGORY;
int       sourceIdx = 0;
int       catSel    = 0;

static bool          btnWasPressed = false;
static unsigned long pressStartMs  = 0;
static bool          longFired     = false;

String  playlist[MAX_STORIES];
String  playlistNames[MAX_STORIES];
int     totalStories    = 0;
int     currentStory    = 0;
bool    isPlaying       = false;
bool    audioWasRunning = false;

static unsigned long lastPressMs     = 0;
static unsigned long lastReleaseMs   = 0;
static int           clickCount      = 0;
static bool          btnDown         = false;
static bool          playerLongFired = false;

static uint32_t lastBarSec = 0xFFFFFFFF;

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
        if (millis() - pressStartMs >= BTN_DEBOUNCE_MS) event = 1;
    }
    btnWasPressed = pressed;
    return event;
}

void drawProgressBar(uint32_t currentSec, uint32_t totalSec) {
    if (currentSec == lastBarSec) return;
    lastBarSec = currentSec;

    const int X = 16, Y = 172, W = 208, H = 8;
    float pct = (totalSec > 0) ? (float)currentSec / totalSec : 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    int fillW = (int)(pct * W);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u:%02u / %u:%02u",
             currentSec / 60, currentSec % 60,
             totalSec  / 60, totalSec  % 60);

    tft.fillRect(0, 148, SCREEN_W, 52, BG_COLOR);

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(16, 153);
    tft.print(buf);

    tft.drawRect(X, Y, W, H, ST77XX_WHITE);
    if (fillW > 0) {
        uint16_t barColor = (pct >= 0.95f) ? ST77XX_GREEN : ST77XX_CYAN;
        tft.fillRect(X + 1, Y + 1, fillW - 2, H - 2, barColor);
    }

    snprintf(buf, sizeof(buf), "%d%%", (int)(pct * 100));
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(200, 153);
    tft.print(buf);
}

void drawPlayer(const String& title, const String& status, const String& instruction) {
    lastBarSec = 0xFFFFFFFF;

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

    tft.fillRect(0, 148, SCREEN_W, 52, BG_COLOR);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_ORANGE);
    tft.setCursor(10, 200);
    tft.println(instruction);
}

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
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject v : arr) {
                String n = v["name"].as<String>();
                if (n.endsWith(".mp3") && totalStories < MAX_STORIES) {
                    playlist[totalStories]      = String(SOURCES[sourceIdx].pagesBase) + n;
                    playlistNames[totalStories] = n;
                    totalStories++;
                }
            }
            Serial.printf("[STORY] Loaded %d items\n", totalStories);
        } else {
            Serial.printf("[STORY] JSON parse error: %s\n", err.c_str());
        }
    }
    http.end();
}

void playStory() {
    if (totalStories == 0) {
        Serial.println("[STORY] No items loaded");
        drawPlayer("No items", "Load failed", "1x: Retry");
        return;
    }
    Serial.printf("[STORY] Playing: %s\n", playlist[currentStory].c_str());
    audio.connecttohost(playlist[currentStory].c_str());
    isPlaying       = true;
    audioWasRunning = false;
    drawPlayer(playlistNames[currentStory], "Playing", "1x:P/P 2x:Next 3x:Prev");
}

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

void enterCategory() {
    screen = SCR_CATEGORY;
    catSel = 0;
    btnWasPressed = true;
    longFired     = true;
    drawCategory(catSel);
}

void startPlayer() {
    audio.setPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN);
    audio.setVolume(21);
    audio.forceMono(true);

    currentStory    = 0;
    isPlaying       = false;
    audioWasRunning = false;
    clickCount      = 0;
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

void backToCategory() {
    audio.stopSong();
    isPlaying       = false;
    audioWasRunning = false;
    clickCount      = 0;
    btnWasPressed = true;
    longFired     = true;
    screen = SCR_CATEGORY;
    catSel = sourceIdx;
    drawCategory(catSel);
}

void handlePlayerButton() {
    unsigned long now = millis();
    bool pressed = (digitalRead(PIN_BTN) == LOW);

    if (pressed && !btnDown) {
        btnDown         = true;
        lastPressMs     = now;
        playerLongFired = false;
    }

    if (pressed && btnDown && !playerLongFired && (now - lastPressMs >= BTN_LONG_MS)) {
        playerLongFired = true;
        backToCategory();
        return;
    }

    if (!pressed && btnDown) {
        btnDown = false;
        if (!playerLongFired && (now - lastPressMs >= BTN_DEBOUNCE_MS)) {
            lastReleaseMs = now;
            clickCount++;
        }
    }

    if (clickCount > 0 && !btnDown && (now - lastReleaseMs > 450)) {
        if (clickCount == 1) {
            if (totalStories == 0) {
                startPlayer();
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

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== HS-01 Storyteller ===");

    pinMode(PIN_BTN, INPUT_PULLUP);

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tftSPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
    tft.init(SCREEN_W, SCREEN_H, SPI_MODE3);
    tft.setRotation(0);
    tft.fillScreen(BG_COLOR);

    hs01::WifiSelect wifi(tft, PIN_BTN, WIFI_CONFIGS, WIFI_COUNT, BG_COLOR);
    wifi.connect();

    enterCategory();
}

void loop() {
    if (screen == SCR_PLAYER) {
        audio.loop();

        bool running = audio.isRunning();
        if (isPlaying && audioWasRunning && !running && totalStories > 0) {
            currentStory = (currentStory + 1) % totalStories;
            playStory();
        }
        audioWasRunning = running;

        if (isPlaying && running) {
            uint32_t cur = audio.getAudioCurrentTime();
            uint32_t dur = audio.getAudioFileDuration();
            if (dur > 0) drawProgressBar(cur, dur);
        }

        handlePlayerButton();
        return;
    }

    int btn = readButton();
    if (screen == SCR_CATEGORY) {
        if (btn == 1) {
            catSel = (catSel + 1) % SOURCE_COUNT;
            drawCategory(catSel);
        } else if (btn == 2) {
            sourceIdx = catSel;
            Serial.printf("[MODE] Selected: %s\n", SOURCES[sourceIdx].name);
            startPlayer();
        }
    }
}
