/*
 * hs01_wifi.h — Shared WiFi selection for HS-01 sketches
 * ─────────────────────────────────────────────────────
 * Drop-in WiFi connect flow for the HS-01 robot (ESP32-S3 + ST7789 TFT +
 * single button). On boot it tries the last-used network automatically; if
 * that fails it shows an on-screen picker driven by ONE button:
 *
 *   Short press (<1s)  → highlight the next entry
 *   Long press  (≥1s)  → connect to / activate the highlighted entry
 *
 * The picker lists your hard-coded networks PLUS a final "Phone Setup" entry
 * that launches a WiFiManager captive portal: connect your phone to the AP,
 * a config page opens, pick any network and type the password. Credentials
 * chosen that way are remembered and retried first on the next boot.
 *
 * Required libraries:
 *   • WiFiManager (tzapu)
 *   • Adafruit ST7735/ST7789 + Adafruit GFX
 *
 * Usage (in a sketch):
 *   #include "../shared/hs01_wifi.h"
 *   const hs01::WifiConfig NETS[] = {
 *       { "Home", "MySSID", "mypassword" },
 *   };
 *   hs01::WifiSelect wifi(tft, PIN_BTN, NETS,
 *                         sizeof(NETS)/sizeof(NETS[0]), BG_COLOR);
 *   // in setup(), after tft.init():
 *   wifi.connect();     // blocks until WiFi is connected
 */
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFiManager.h>

namespace hs01 {

struct WifiConfig {
    const char* name;      // shown on screen (ASCII only)
    const char* ssid;
    const char* password;
};

class WifiSelect {
public:
    WifiSelect(Adafruit_ST7789& tft, int btnPin,
               const WifiConfig* configs, int count,
               uint16_t bgColor = 0x0014,          // navy blue
               const char* apName = "HS-01-Setup")
        : _tft(tft), _btn(btnPin), _cfg(configs), _n(count),
          _bg(bgColor), _apName(apName) {}

    // Full flow — blocks until connected.
    void connect() {
        _tft.setTextWrap(false);

        // 1) Try credentials saved from a previous phone (AP) setup.
        String apSsid, apPass;
        loadApCreds(apSsid, apPass);
        if (apSsid.length() > 0) {
            Serial.printf("[WIFI] Trying saved phone creds: %s\n", apSsid.c_str());
            drawConnecting(apSsid.c_str(), 0);
            if (tryConnectRaw(apSsid.c_str(), apPass.c_str(), 8000)) {
                onConnected("Phone WiFi");
                return;
            }
            Serial.println("[WIFI] Saved phone creds failed — falling back");
        }

        // 2) Try the last-used hard-coded network.
        int sel = clampIdx(loadLastIdx());
        if (_n > 0) {
            Serial.printf("[WIFI] Trying last hard-coded net: %s\n", _cfg[sel].ssid);
            drawConnecting(_cfg[sel].ssid, 0);
            if (tryConnectRaw(_cfg[sel].ssid, _cfg[sel].password, 8000)) {
                onConnected(_cfg[sel].name);
                return;
            }
        }

        // 3) Fall back to the interactive picker.
        runPicker(sel);
    }

private:
    // ── item helpers (hard-coded nets + one trailing "Phone Setup") ──────────
    int  total()        const { return _n + 1; }          // +1 AP entry
    bool isApRow(int i) const { return i == _n; }
    const char* itemName(int i) const { return isApRow(i) ? "Phone Setup" : _cfg[i].name; }
    const char* itemHint(int i) const { return isApRow(i) ? "via phone"   : _cfg[i].ssid; }

    // ── picker loop ──────────────────────────────────────────────────────────
    void runPicker(int sel) {
        drawPicker(sel);
        for (;;) {
            int ev = readButton();
            if (ev == 1) {
                sel = (sel + 1) % total();
                drawPicker(sel);
            } else if (ev == 2) {
                if (isApRow(sel)) {
                    if (runApPortal()) {
                        onConnected("Phone WiFi");
                        return;
                    }
                    drawError("Phone Setup");
                } else {
                    drawConnecting(_cfg[sel].ssid, 0);
                    if (tryConnectRaw(_cfg[sel].ssid, _cfg[sel].password, 15000)) {
                        saveLastIdx(sel);
                        onConnected(_cfg[sel].name);
                        return;
                    }
                    drawError(_cfg[sel].name);
                }
                waitForAnyPress();
                drawPicker(sel);
            }
            delay(5);
        }
    }

    void onConnected(const char* label) {
        drawConnected(label, WiFi.localIP().toString().c_str());
        delay(1500);
    }

    // ── WiFiManager captive portal ─────────────────────────────────────────
    // Returns true if the user configured a network and WiFi is connected.
    // Saves credentials via WiFiManager's own getters BEFORE wm is destroyed,
    // because WiFi.psk() is unreliable on ESP32-S3.
    bool runApPortal() {
        drawApMode();
        WiFiManager wm;
        wm.setConfigPortalTimeout(180);
        wm.setBreakAfterConfig(true);
        wm.startConfigPortal(_apName);

        if (WiFi.status() == WL_CONNECTED) {
            String ssid = WiFi.SSID();
            String pass = wm.getWiFiPass();
            if (pass.length() == 0) pass = WiFi.psk();
            Serial.printf("[WIFI] AP portal captured: SSID=\"%s\" pass_len=%d\n",
                          ssid.c_str(), pass.length());
            saveApCreds(ssid, pass);
            return true;
        }
        return false;
    }

    // ── WiFi connect primitive ───────────────────────────────────────────────
    bool tryConnectRaw(const char* ssid, const char* pass, unsigned long timeoutMs) {
        Serial.printf("[WIFI] Connecting to \"%s\"\n", ssid);
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);

        unsigned long start = millis();
        int dots = 0;
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > timeoutMs) {
                Serial.println("[WIFI] Timeout");
                return false;
            }
            drawConnecting(ssid, dots++);
            delay(250);
        }
        Serial.printf("[WIFI] Connected! IP=%s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    // ── button: 0=nothing 1=short 2=long ─────────────────────────────────────
    int readButton() {
        bool pressed = (digitalRead(_btn) == LOW);
        int  event   = 0;
        if (pressed && !_wasPressed) { _pressStart = millis(); _longFired = false; }
        if (pressed && !_longFired && (millis() - _pressStart >= 1000)) {
            _longFired = true; event = 2;
        }
        if (!pressed && _wasPressed && !_longFired) {
            if (millis() - _pressStart >= 50) event = 1;
        }
        _wasPressed = pressed;
        return event;
    }

    void waitForAnyPress() {
        // require release first, then a fresh press
        while (digitalRead(_btn) == LOW) delay(10);
        while (digitalRead(_btn) == HIGH) delay(10);
        _wasPressed = true; _longFired = true;   // swallow this press
    }

    // ── persistence (Preferences namespace "hs01wifi") ──────────────────────
    // NOTE: saveLastIdx() does NOT clear AP creds. AP creds persist
    // independently so that after using a hard-coded network you can
    // still auto-connect with a previously phone-saved network next boot.
    int  loadLastIdx() {
        Preferences p; p.begin("hs01wifi", true);
        int v = p.getInt("lastIdx", 0); p.end(); return v;
    }
    void saveLastIdx(int idx) {
        Preferences p; p.begin("hs01wifi", false);
        p.putInt("lastIdx", idx); p.end();
    }
    void loadApCreds(String& ssid, String& pass) {
        Preferences p; p.begin("hs01wifi", true);
        ssid = p.getString("apSsid", "");
        pass = p.getString("apPass", "");
        Serial.printf("[WIFI] Loaded AP creds: SSID=\"%s\" pass_len=%d\n",
                      ssid.c_str(), pass.length());
        p.end();
    }
    void saveApCreds(const String& ssid, const String& pass) {
        Preferences p; p.begin("hs01wifi", false);
        p.putString("apSsid", ssid);
        p.putString("apPass", pass);
        Serial.printf("[WIFI] Saved AP creds: SSID=\"%s\" pass_len=%d\n",
                      ssid.c_str(), pass.length());
        p.end();
    }
    int clampIdx(int i) { return (i < 0 || i >= _n) ? 0 : i; }

    // ── screens ──────────────────────────────────────────────────────────────
    void header(const char* title, uint16_t color) {
        _tft.fillScreen(_bg);
        _tft.setTextSize(3);
        _tft.setTextColor(color);
        _tft.setCursor(10, 18);
        _tft.println(title);
        _tft.drawLine(0, 55, W, 55, ST77XX_WHITE);
    }

    void drawConnecting(const char* ssid, int dots) {
        header("Connecting", ST77XX_YELLOW);
        _tft.setTextSize(2);
        _tft.setTextColor(ST77XX_CYAN);
        _tft.setCursor(10, 75);
        _tft.println("SSID:");
        _tft.setTextColor(ST77XX_WHITE);
        _tft.setCursor(10, 100);
        _tft.println(ssid);
        _tft.setTextSize(3);
        _tft.setTextColor(ST77XX_WHITE);
        _tft.setCursor(10, 160);
        for (int i = 0; i <= dots % 4; i++) _tft.print(".");
    }

    void drawPicker(int highlight) {
        _tft.fillScreen(_bg);
        _tft.setTextSize(2);
        _tft.setTextColor(ST77XX_YELLOW);
        _tft.setCursor(10, 12);
        _tft.println("Select WiFi:");
        _tft.drawLine(0, 40, W, 40, ST77XX_WHITE);

        const int rowH = 42, startY = 50, visible = 4;
        int first = max(0, min(highlight, total() - visible));
        for (int i = first; i < min(first + visible, total()); i++) {
            int y = startY + (i - first) * rowH;
            uint16_t nameColor;
            if (i == highlight) {
                _tft.fillRect(0, y, W, rowH - 4, ST77XX_WHITE);
                nameColor = ST77XX_BLACK;
            } else {
                nameColor = isApRow(i) ? ST77XX_GREEN : ST77XX_WHITE;
            }
            _tft.setTextSize(2);
            _tft.setTextColor(nameColor);
            _tft.setCursor(8, y + 4);
            _tft.print(i == highlight ? "> " : "  ");
            _tft.print(itemName(i));

            _tft.setTextSize(1);
            _tft.setTextColor(i == highlight ? ST77XX_BLACK : ST77XX_CYAN);
            _tft.setCursor(28, y + 24);
            _tft.print(itemHint(i));
        }

        _tft.setTextSize(1);
        _tft.setTextColor(ST77XX_GREEN);
        _tft.setCursor(8, 226);
        _tft.print("Tap: next   Hold: select");
    }

    void drawConnected(const char* name, const char* ip) {
        header("Connected!", ST77XX_GREEN);
        _tft.setTextSize(2);
        _tft.setTextColor(ST77XX_CYAN);
        _tft.setCursor(10, 80);
        _tft.print("WiFi: ");
        _tft.setTextColor(ST77XX_WHITE);
        _tft.println(name);
        _tft.setTextColor(ST77XX_CYAN);
        _tft.setCursor(10, 120);
        _tft.println("IP:");
        _tft.setTextColor(ST77XX_WHITE);
        _tft.setCursor(10, 145);
        _tft.println(ip);
    }

    void drawError(const char* name) {
        header("Failed!", ST77XX_RED);
        _tft.setTextSize(2);
        _tft.setTextColor(ST77XX_CYAN);
        _tft.setCursor(10, 80);
        _tft.print("WiFi: ");
        _tft.setTextColor(ST77XX_WHITE);
        _tft.println(name);
        _tft.setTextColor(ST77XX_ORANGE);
        _tft.setCursor(10, 160);
        _tft.println("Press to retry");
    }

    void drawApMode() {
        header("Phone Setup", ST77XX_YELLOW);
        _tft.setTextSize(2);
        _tft.setTextColor(ST77XX_WHITE);
        _tft.setCursor(10, 70);
        _tft.println("1) Join WiFi:");
        _tft.setTextColor(ST77XX_GREEN);
        _tft.setCursor(10, 92);
        _tft.println(_apName);
        _tft.setTextColor(ST77XX_WHITE);
        _tft.setCursor(10, 128);
        _tft.println("2) Open browser");
        _tft.setCursor(10, 150);
        _tft.println("   then pick WiFi");
        _tft.setTextSize(1);
        _tft.setTextColor(ST77XX_CYAN);
        _tft.setCursor(10, 200);
        _tft.println("Portal times out in 3 min");
    }

    static const int W = 240;

    Adafruit_ST7789& _tft;
    int               _btn;
    const WifiConfig* _cfg;
    int               _n;
    uint16_t          _bg;
    const char*       _apName;

    bool          _wasPressed = false;
    unsigned long _pressStart = 0;
    bool          _longFired  = false;
};

} // namespace hs01
