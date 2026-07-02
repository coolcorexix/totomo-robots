#pragma once

// Fly.io voice server
#define SERVER_HOST     "totomo-voice.fly.dev"
#define SERVER_PORT     80
#define DEVICE_ID       "hs01-chatbot-01"
#define CLIENT_ID       "hs01-chatbot-01"

// ── WiFi list (for the on-screen picker) ────────────────────────
// Last entry is always "Phone Setup" (AP portal via phone).
#define HS01_WIFI_COUNT   2
#define HS01_WIFI_LIST   { \
    { "To To Mo", "To To Mo", "biethoidehoc" }, \
    { "CT1-1009", "Nha Meo Va Phat", "12345678" }, \
}

// ── Pins (matching storyteller sketch) ──────────────────────────
#define PIN_BTN         0

#define TFT_SCLK       21
#define TFT_MOSI       47
#define TFT_DC         40
#define TFT_CS         41
#define TFT_BL         42          // backlight — MUST be HIGH
#define TFT_RST        45

#define TFT_BG         0x0014      // navy blue

#define PIN_MIC_WS      4
#define PIN_MIC_SCK     5
#define PIN_MIC_SD      6

#define PIN_SPK_BCLK    7
#define PIN_SPK_LRC    15
#define PIN_SPK_DIN    16

// ── Audio ────────────────────────────────────────────────────────
#define MIC_SAMPLE_RATE     16000
#define MIC_FRAME_MS        60
#define MIC_FRAME_SAMPLES   (MIC_SAMPLE_RATE * MIC_FRAME_MS / 1000)
#define TTS_SAMPLE_RATE     24000
#define TTS_FRAME_MS        60
#define TTS_FRAME_SAMPLES   (TTS_SAMPLE_RATE * TTS_FRAME_MS / 1000)
#define BTN_LONG_PRESS_MS   1000
