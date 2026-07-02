#pragma once

// ──────────────────────────────────────────────────────────────────
//  Network — picked on-screen (or via phone) on boot
// ──────────────────────────────────────────────────────────────────

// Fly.io voice server  (always-on, free)
#define SERVER_HOST     "totomo-voice.fly.dev"
#define SERVER_PORT     80

#define DEVICE_ID       "hs01-chatbot-01"
#define CLIENT_ID       "hs01-chatbot-01"

// ── Hard-coded networks (shown in the on-screen picker) ──────────
// Add your common WiFi networks here — they appear as quick-select
// entries.  The last entry is always "Phone Setup" (AP portal).
#define HS01_WIFI_COUNT   2
#define HS01_WIFI_LIST   { \
    { "Home 2.4G", "MySSID", "mypassword" }, \
    { "Office",    "OfficeNet", "officepass" }, \
}

// ── Pins ─────────────────────────────────────────────────────────

// INMP441 I2S mic
#define PIN_MIC_WS      4
#define PIN_MIC_SCK     5
#define PIN_MIC_SD      6

// MAX98357A I2S speaker
#define PIN_SPK_BCLK    7
#define PIN_SPK_LRC     15
#define PIN_SPK_DIN     16

// Push button
#define PIN_BTN         0

// ST7789 TFT  (SPI2 default — adjust to your PCB)
#define PIN_TFT_CS      10
#define PIN_TFT_DC      8
#define PIN_TFT_RST     9
#define PIN_TFT_MOSI    11
#define PIN_TFT_SCLK    12
#define TFT_BG          0x0014    // navy-blue background

// ── Audio ────────────────────────────────────────────────────────
#define MIC_SAMPLE_RATE     16000
#define MIC_FRAME_MS        60
#define MIC_FRAME_SAMPLES   (MIC_SAMPLE_RATE * MIC_FRAME_MS / 1000)
#define TTS_SAMPLE_RATE     24000
#define TTS_FRAME_MS        60
#define TTS_FRAME_SAMPLES   (TTS_SAMPLE_RATE * TTS_FRAME_MS / 1000)
#define BTN_LONG_PRESS_MS   1000
