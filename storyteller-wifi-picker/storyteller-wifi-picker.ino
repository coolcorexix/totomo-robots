/*
 * WiFi Multi-Config Picker — Totomo Storyteller Box
 * ─────────────────────────────────────────────────
 * On boot the device tries the last-used WiFi automatically.
 * If it fails, a picker appears on the OLED so you can choose
 * another network without reflashing.
 *
 * Controls (single button):
 *   Short press  (<1 s)  → cycle to the next network in the list
 *   Long press   (≥1 s)  → connect to the highlighted network
 *   Any press on error   → go back to picker
 *
 * Hardware assumed:
 *   • ESP32 or ESP32-S3
 *   • SSD1306 OLED 128×64, I2C (SDA=21, SCL=22 on standard ESP32)
 *   • Push button on PIN_BTN (active LOW, uses built-in pull-up)
 *
 * Required libraries (install via Arduino Library Manager):
 *   • Adafruit SSD1306
 *   • Adafruit GFX Library
 *
 * ── EDIT SECTION ──────────────────────────────────────────────
 * Only change WIFI_CONFIGS[] and the pin/display constants below.
 * Everything else is self-contained.
 */

#include <WiFi.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── Pin & display ────────────────────────────────────────────────────────────
#define PIN_BTN      0       // Boot button on most ESP32 dev boards (active LOW)
#define OLED_SDA    21
#define OLED_SCL    22
#define OLED_ADDR   0x3C    // Try 0x3D if display stays blank
#define SCREEN_W   128
#define SCREEN_H    64

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
    { "Dien thoai", "Hotspot_Phat",  "hotspot_password"  },
};
const int WIFI_COUNT = sizeof(WIFI_CONFIGS) / sizeof(WIFI_CONFIGS[0]);

// ─── Globals ──────────────────────────────────────────────────────────────────
Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);
Preferences       prefs;

enum AppScreen { SCR_CONNECTING, SCR_PICKER, SCR_CONNECTED, SCR_ERROR };
AppScreen screen    = SCR_CONNECTING;
int       selIdx    = 0;

// Button tracking
static bool         btnWasPressed = false;
static unsigned long pressStartMs = 0;
static bool         longFired     = false;

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
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);

    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("Dang ket noi...");
    oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    oled.setCursor(0, 16);
    oled.print("SSID: ");
    oled.println(ssid);

    oled.setCursor(0, 48);
    for (int i = 0; i <= dots % 4; i++) oled.print(".");
    oled.display();
}

void drawPicker(int highlight) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);

    oled.setCursor(0, 0);
    oled.println("Chon mang WiFi:");
    oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    // Show up to 3 items (fits 128x64 at 1x scale)
    int firstVisible = max(0, min(highlight, WIFI_COUNT - 3));
    for (int i = firstVisible; i < min(firstVisible + 3, WIFI_COUNT); i++) {
        int row = i - firstVisible;
        int y   = 14 + row * 14;

        if (i == highlight) {
            oled.fillRect(0, y - 1, 128, 13, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.setTextColor(SSD1306_WHITE);
        }

        oled.setCursor(4, y);
        oled.print(i == highlight ? ">" : " ");
        oled.print(" ");
        oled.print(WIFI_CONFIGS[i].name);

        // Show truncated SSID on the right as a hint
        char hint[10];
        const char* s   = WIFI_CONFIGS[i].ssid;
        int         len = strlen(s);
        snprintf(hint, sizeof(hint), "%s", len > 8 ? s + len - 8 : s);
        oled.setCursor(128 - strlen(hint) * 6 - 2, y);
        oled.print(hint);
    }

    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 57);
    oled.print("Nhan giu de ket noi");
    oled.display();
}

void drawConnected(const char* name, const char* ip) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);

    oled.setCursor(0, 0);
    oled.println("Da ket noi!");
    oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    oled.setCursor(0, 18);
    oled.print("Mang: ");
    oled.println(name);
    oled.setCursor(0, 32);
    oled.print("IP:   ");
    oled.println(ip);

    oled.display();
}

void drawError(const char* name) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);

    oled.setCursor(0, 0);
    oled.println("Ket noi that bai!");
    oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    oled.setCursor(0, 18);
    oled.print("Mang: ");
    oled.println(name);

    oled.setCursor(0, 48);
    oled.println("Nhan de thu lai...");
    oled.display();
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

// ─── setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Totomo WiFi Picker ===");

    pinMode(PIN_BTN, INPUT_PULLUP);

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[OLED] Init failed — check wiring/address");
        // Carry on; device works without display (just no visual feedback)
    }
    oled.clearDisplay();
    oled.display();

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
        screen = SCR_CONNECTED;
        delay(1500);

        // ── Hand off to main storyteller logic below ──────────────────────
        // TODO: replace with your app's init code after WiFi is up
        // e.g. connectToServer(), startAudio(), etc.
        return;
    }

    Serial.println("[WIFI] Auto-connect failed — showing picker");
    screen = SCR_PICKER;
    drawPicker(selIdx);
}

// ─── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    // ── WiFi connected — main app runs here ──────────────────────────────────
    if (screen == SCR_CONNECTED) {
        // TODO: put your storyteller app loop logic here
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
                screen = SCR_CONNECTED;
                delay(1500);

                // ── Hand off to main storyteller logic ────────────────────
                // TODO: same as in setup() above
            } else {
                Serial.printf("[WIFI] Failed to connect to %s\n", WIFI_CONFIGS[selIdx].ssid);
                screen = SCR_ERROR;
                drawError(WIFI_CONFIGS[selIdx].name);
            }
        }

    } else if (screen == SCR_ERROR) {
        if (btn != 0) {
            // Any button press → back to picker
            screen = SCR_PICKER;
            drawPicker(selIdx);
        }
    }
}
