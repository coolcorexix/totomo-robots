#pragma once

// ──────────────────────────────────────────────────────────────────
//  Network — fill in before flashing
// ──────────────────────────────────────────────────────────────────
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASSWORD   "your_wifi_password"

// Fly.io voice server  (always-on, free)
#define SERVER_HOST     "totomo-voice.fly.dev"
#define SERVER_PORT     80

#define DEVICE_ID       "totomo-clock-01"
#define CLIENT_ID       "totomo-clock-01"

// NTP
#define NTP_SERVER      "pool.ntp.org"
#define TZ_OFFSET_SEC   (7 * 3600)

// ──────────────────────────────────────────────────────────────────
//  Pin mapping  (match your PCB)
// ──────────────────────────────────────────────────────────────────
#define PIN_MIC_WS      4
#define PIN_MIC_SCK     5
#define PIN_MIC_SD      6
#define PIN_SPK_BCLK    7
#define PIN_SPK_LRC     15
#define PIN_SPK_DIN     16
#define PIN_BTN         0

// ──────────────────────────────────────────────────────────────────
//  Audio
// ──────────────────────────────────────────────────────────────────
#define MIC_SAMPLE_RATE     16000
#define MIC_FRAME_MS        60
#define MIC_FRAME_SAMPLES   (MIC_SAMPLE_RATE * MIC_FRAME_MS / 1000)
#define TTS_SAMPLE_RATE     24000
#define TTS_FRAME_MS        60
#define TTS_FRAME_SAMPLES   (TTS_SAMPLE_RATE * TTS_FRAME_MS / 1000)

#define BTN_LONG_PRESS_MS   1000
