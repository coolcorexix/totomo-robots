#pragma once

// Voice server: Fly.io by default; the `local` PlatformIO env overrides these
// to point at a server on the LAN (see platformio.ini).
#ifndef SERVER_HOST
#define SERVER_HOST     "totomo-voice.fly.dev"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT     80
#endif
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

// NOTE: matches the corrected HS01_HARDWARE.md pinout (verified on real
// hardware, storyteller.ino) — this file previously had BCLK/DIN swapped,
// which produces total silence with zero errors logged (decode succeeds
// fine — the bug is purely in which physical pin gets which I2S signal).
#define PIN_SPK_DIN     7
#define PIN_SPK_BCLK   15
#define PIN_SPK_LRC    16

// ── Audio ────────────────────────────────────────────────────────
#define MIC_SAMPLE_RATE     16000
#define MIC_FRAME_MS        60
#define MIC_FRAME_SAMPLES   (MIC_SAMPLE_RATE * MIC_FRAME_MS / 1000)
#define MIC_GAIN            2.0f      // on top of 24->16 bit; ~-21..-28 dBFS speech
#define MIC_HPF_A           0.9615f   // DC blocker, exp(-2*pi*100Hz/16kHz)
#define CONVO_IDLE_MS       120000    // conversation ends after 2 min without speech
#define MIC_RESUME_MS       400       // mic stays muted this long after the robot speaks
#define TTS_SAMPLE_RATE     24000
#define TTS_FRAME_MS        60
#define TTS_FRAME_SAMPLES   (TTS_SAMPLE_RATE * TTS_FRAME_MS / 1000)
