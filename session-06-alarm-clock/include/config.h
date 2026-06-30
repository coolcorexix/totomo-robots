#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Network — fill in before flashing
// ─────────────────────────────────────────────────────────────────────────────
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASSWORD   "your_wifi_password"

// IP of the Mac running totomo-voice-server  (devenv up → port 8000)
#define SERVER_HOST     "192.168.1.100"
#define SERVER_PORT     8000

// Unique IDs for this device (any stable string is fine)
#define DEVICE_ID       "totomo-clock-01"
#define CLIENT_ID       "totomo-clock-01"

// NTP
#define NTP_SERVER      "pool.ntp.org"
#define TZ_OFFSET_SEC   (7 * 3600)   // UTC+7 (Vietnam)

// ─────────────────────────────────────────────────────────────────────────────
//  Pin mapping — adjust to match your PCB schematic
// ─────────────────────────────────────────────────────────────────────────────

// INMP441 I2S microphone (I2S_NUM_0)
#define PIN_MIC_WS      4    // Word Select (L/R clock)
#define PIN_MIC_SCK     5    // Bit clock
#define PIN_MIC_SD      6    // Serial data (SD / DOUT)

// MAX98357A I2S amplifier / speaker (I2S_NUM_1)
#define PIN_SPK_BCLK    7    // Bit clock
#define PIN_SPK_LRC     15   // Left/Right clock
#define PIN_SPK_DIN     16   // Data in

// Push button (active LOW, uses internal pull-up)
#define PIN_BTN         0    // Boot button on most ESP32-S3 dev boards

// ─────────────────────────────────────────────────────────────────────────────
//  Audio parameters
// ─────────────────────────────────────────────────────────────────────────────
#define MIC_SAMPLE_RATE     16000   // 16 kHz mono PCM sent to server
#define MIC_FRAME_MS        60      // frame duration the server expects
#define MIC_FRAME_SAMPLES   (MIC_SAMPLE_RATE * MIC_FRAME_MS / 1000)  // 960

#define TTS_SAMPLE_RATE     24000   // EdgeTTS → Opus is 24 kHz
#define TTS_FRAME_MS        60
#define TTS_FRAME_SAMPLES   (TTS_SAMPLE_RATE * TTS_FRAME_MS / 1000)  // 1440

// ─────────────────────────────────────────────────────────────────────────────
//  Button timing
// ─────────────────────────────────────────────────────────────────────────────
#define BTN_LONG_PRESS_MS   1000    // hold ≥ 1 s → abort / cancel
