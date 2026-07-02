/**
 * Session 06 — Smart Alarm Clock
 * ESP32-S3 + INMP441 mic + MAX98357A speaker
 *
 * Protocol: xiaozhi WebSocket (same as totomo-voice-server expects)
 *   1. Connect WiFi → sync NTP time
 *   2. Open WebSocket to ws://SERVER/xiaozhi/v1/?device-id=...&client-id=...
 *   3. Send hello JSON (announces PCM 16 kHz audio format)
 *   4. Hold button → stream mic PCM → release → server runs STT + LLM
 *   5. Server sends back Opus TTS frames → decode → play on speaker
 *   6. Server sends alarm list via {type:"alarm",action:"sync"} → store locally
 *   7. When alarm fires → ring tone + wait for server "ring" or local timeout
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <time.h>
#include <opus.h>   // from arduino-libopus
#include "config.h"

// ─────────────────────────────────────────────────────────────────────────────
//  State machine
// ─────────────────────────────────────────────────────────────────────────────
enum State { IDLE, LISTENING, PROCESSING, SPEAKING };
static State state = IDLE;

// ─────────────────────────────────────────────────────────────────────────────
//  WebSocket
// ─────────────────────────────────────────────────────────────────────────────
static WebSocketsClient ws;
static String  sessionId    = "";
static bool    wsConnected  = false;
static bool    helloAcked   = false;

// ─────────────────────────────────────────────────────────────────────────────
//  Alarm storage (server pushes the list via "alarm"/"sync" messages)
// ─────────────────────────────────────────────────────────────────────────────
struct Alarm {
    int  hour;
    int  minute;
    char label[64];
};
static Alarm alarms[16];
static int   alarmCount  = 0;
static bool  ringing     = false;
static unsigned long ringStartMs = 0;
#define RING_DURATION_MS 30000   // ring for 30 s if not dismissed

// ─────────────────────────────────────────────────────────────────────────────
//  I2S mic (input, I2S_NUM_0) — INMP441 outputs 32-bit frames, top 16 used
// ─────────────────────────────────────────────────────────────────────────────
static void i2s_mic_init() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = MIC_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = PIN_MIC_SCK,
        .ws_io_num    = PIN_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_MIC_SD,
    };
    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  I2S speaker (output, I2S_NUM_1) — MAX98357A, 24 kHz for TTS
// ─────────────────────────────────────────────────────────────────────────────
static void i2s_spk_init() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = TTS_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };
    i2s_pin_config_t pins = {
        .bck_io_num   = PIN_SPK_BCLK,
        .ws_io_num    = PIN_SPK_LRC,
        .data_out_num = PIN_SPK_DIN,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_1, &pins);
    i2s_zero_dma_buffer(I2S_NUM_1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Opus decoder (for TTS audio coming from the server)
// ─────────────────────────────────────────────────────────────────────────────
static OpusDecoder* opusDec = nullptr;
static int16_t      opusPcm[TTS_FRAME_SAMPLES * 2]; // stereo headroom

static void opus_init() {
    int err;
    opusDec = opus_decoder_create(TTS_SAMPLE_RATE, 1, &err);
    if (err != OPUS_OK) {
        Serial.printf("[OPUS] decoder init failed: %d\n", err);
    }
}

static void opus_play_packet(const uint8_t* data, size_t len) {
    if (!opusDec) return;
    int samples = opus_decode(opusDec, data, (opus_int32)len,
                              opusPcm, TTS_FRAME_SAMPLES, 0);
    if (samples <= 0) {
        Serial.printf("[OPUS] decode error: %d\n", samples);
        return;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_1, opusPcm, samples * sizeof(int16_t), &written, portMAX_DELAY);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Ring tone — simple square-wave beep via I2S speaker
// ─────────────────────────────────────────────────────────────────────────────
static void play_beep(int freqHz, int durationMs) {
    const int sampleRate = TTS_SAMPLE_RATE;
    const int totalSamples = sampleRate * durationMs / 1000;
    const int period = sampleRate / freqHz;
    static int16_t buf[512];
    int written_total = 0;
    while (written_total < totalSamples) {
        int batch = min((int)sizeof(buf) / 2, totalSamples - written_total);
        for (int i = 0; i < batch; i++) {
            buf[i] = ((written_total + i) % period < period / 2) ? 8000 : -8000;
        }
        size_t w = 0;
        i2s_write(I2S_NUM_1, buf, batch * sizeof(int16_t), &w, portMAX_DELAY);
        written_total += batch;
    }
}

static void ring_alarm() {
    // Three short beeps (440 Hz — A4)
    play_beep(440, 300);
    delay(100);
    play_beep(440, 300);
    delay(100);
    play_beep(660, 500);
    Serial.println("[ALARM] 🔔 Reo chuông!");
}

// ─────────────────────────────────────────────────────────────────────────────
//  JSON helpers
// ─────────────────────────────────────────────────────────────────────────────
static void ws_send_json(JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    ws.sendTXT(out);
}

static void send_hello() {
    StaticJsonDocument<256> doc;
    doc["type"]      = "hello";
    doc["version"]   = 1;
    doc["transport"] = "websocket";
    doc["features"]  = JsonObject();
    JsonObject ap    = doc.createNestedObject("audio_params");
    ap["format"]        = "pcm";
    ap["sample_rate"]   = MIC_SAMPLE_RATE;
    ap["channels"]      = 1;
    ap["frame_duration"] = MIC_FRAME_MS;
    ws_send_json(doc);
    Serial.println("[WS] → hello");
}

static void send_listen_start() {
    StaticJsonDocument<128> doc;
    doc["type"]       = "listen";
    doc["state"]      = "start";
    doc["mode"]       = "manual";
    doc["session_id"] = sessionId;
    ws_send_json(doc);
}

static void send_listen_stop() {
    StaticJsonDocument<128> doc;
    doc["type"]       = "listen";
    doc["state"]      = "stop";
    doc["session_id"] = sessionId;
    ws_send_json(doc);
}

static void send_abort() {
    StaticJsonDocument<64> doc;
    doc["type"]       = "abort";
    doc["session_id"] = sessionId;
    ws_send_json(doc);
    Serial.println("[WS] → abort");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Alarm helpers
// ─────────────────────────────────────────────────────────────────────────────
static void parse_alarm_list(JsonArray arr) {
    alarmCount = 0;
    for (JsonObject a : arr) {
        if (alarmCount >= (int)(sizeof(alarms) / sizeof(alarms[0]))) break;
        const char* timeStr = a["time"] | "";
        // timeStr format: "YYYY-MM-DD HH:MM"
        int h = -1, m = -1;
        if (strlen(timeStr) >= 16) {
            h = atoi(timeStr + 11);
            m = atoi(timeStr + 14);
        }
        if (h < 0 || h > 23 || m < 0 || m > 59) continue;
        alarms[alarmCount].hour   = h;
        alarms[alarmCount].minute = m;
        strlcpy(alarms[alarmCount].label, a["label"] | "", sizeof(alarms[0].label));
        alarmCount++;
    }
    Serial.printf("[ALARM] Stored %d alarm(s)\n", alarmCount);
}

static void check_alarms() {
    if (ringing || alarmCount == 0) return;
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    for (int i = 0; i < alarmCount; i++) {
        if (t->tm_hour == alarms[i].hour && t->tm_min == alarms[i].minute) {
            ringing      = true;
            ringStartMs  = millis();
            Serial.printf("[ALARM] FIRING: %02d:%02d %s\n",
                          alarms[i].hour, alarms[i].minute, alarms[i].label);
            ring_alarm();
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  WebSocket event handler
// ─────────────────────────────────────────────────────────────────────────────
static void on_ws_event(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {

    case WStype_CONNECTED:
        wsConnected = true;
        helloAcked  = false;
        Serial.println("[WS] Connected → sending hello");
        send_hello();
        break;

    case WStype_DISCONNECTED:
        wsConnected  = false;
        helloAcked   = false;
        sessionId    = "";
        state        = IDLE;
        Serial.println("[WS] Disconnected");
        break;

    case WStype_TEXT: {
        StaticJsonDocument<1024> doc;
        if (deserializeJson(doc, payload, length) != DeserializationError::Ok) break;
        const char* msgType = doc["type"] | "";

        if (strcmp(msgType, "hello") == 0) {
            // Server confirms session
            sessionId  = doc["session_id"] | "";
            helloAcked = true;
            Serial.printf("[WS] hello ack · session=%s\n", sessionId.c_str());

        } else if (strcmp(msgType, "stt") == 0) {
            const char* text = doc["text"] | "";
            if (text[0] && text[0] != '%') {   // '%' = internal tool trace
                Serial.printf("[STT] \"%s\"\n", text);
                state = PROCESSING;
            }

        } else if (strcmp(msgType, "llm") == 0) {
            // Emotion emoji — logged for debug
            Serial.printf("[LLM] emotion: %s\n", doc["text"] | "");

        } else if (strcmp(msgType, "tts") == 0) {
            const char* ttsState = doc["state"] | "";
            if (strcmp(ttsState, "start") == 0) {
                state = SPEAKING;
                Serial.println("[TTS] start");
            } else if (strcmp(ttsState, "sentence_start") == 0) {
                Serial.printf("[TTS] \"%s\"\n", doc["text"] | "");
            } else if (strcmp(ttsState, "stop") == 0) {
                state = IDLE;
                Serial.println("[TTS] stop → IDLE");
            }

        } else if (strcmp(msgType, "alarm") == 0) {
            const char* action = doc["action"] | "";
            if (strcmp(action, "sync") == 0) {
                parse_alarm_list(doc["alarms"].as<JsonArray>());
            } else if (strcmp(action, "ring") == 0) {
                // Server explicitly asks device to ring (scheduler built later)
                ringing     = true;
                ringStartMs = millis();
                ring_alarm();
            }
        }
        break;
    }

    case WStype_BIN:
        // Binary frame = Opus-encoded TTS audio from server
        if (state == SPEAKING) {
            opus_play_packet(payload, length);
        }
        break;

    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Button — hold to talk, release to send; long-press to abort
// ─────────────────────────────────────────────────────────────────────────────
static bool         btnPrev      = HIGH;
static unsigned long btnPressAt  = 0;
static bool         pttActive    = false;

static void handle_button() {
    bool cur = digitalRead(PIN_BTN);

    // Falling edge — button pressed
    if (btnPrev == HIGH && cur == LOW) {
        btnPressAt = millis();
        btnPrev    = LOW;
    }

    // Rising edge — button released
    if (btnPrev == LOW && cur == HIGH) {
        unsigned long held = millis() - btnPressAt;
        btnPrev = HIGH;

        if (held >= BTN_LONG_PRESS_MS) {
            // Long press: abort whatever is happening
            if (ringing) {
                ringing = false;
                Serial.println("[BTN] alarm dismissed");
            } else if (state != IDLE) {
                send_abort();
                state = IDLE;
            }
        } else if (pttActive) {
            // Normal release: stop listening
            pttActive = false;
            send_listen_stop();
            state = PROCESSING;
            Serial.println("[BTN] PTT stop");
        }
    }

    // While held: start listening once hello is acked and we are idle
    if (cur == LOW && !pttActive && helloAcked && state == IDLE) {
        if ((millis() - btnPressAt) > 50) {   // 50 ms debounce
            pttActive = true;
            state     = LISTENING;
            send_listen_start();
            Serial.println("[BTN] PTT start");
        }
    }

    btnPrev = cur;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mic streaming — called every loop tick when in LISTENING state
// ─────────────────────────────────────────────────────────────────────────────
// INMP441 gives 32-bit frames; we take the upper 16 bits (the useful part).
static int32_t  micRaw[MIC_FRAME_SAMPLES];
static int16_t  micPcm[MIC_FRAME_SAMPLES];

static void stream_mic_frame() {
    size_t bytesRead = 0;
    esp_err_t ret = i2s_read(I2S_NUM_0, micRaw, sizeof(micRaw), &bytesRead, 0);
    if (ret != ESP_OK || bytesRead == 0) return;
    int samples = bytesRead / 4;   // 32-bit → 4 bytes each
    for (int i = 0; i < samples; i++) {
        micPcm[i] = (int16_t)(micRaw[i] >> 11);   // 32→16 bit, slight boost
    }
    ws.sendBIN((const uint8_t*)micPcm, samples * sizeof(int16_t));
}

// ─────────────────────────────────────────────────────────────────────────────
//  WiFi + NTP setup
// ─────────────────────────────────────────────────────────────────────────────
static void wifi_connect() {
    Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WIFI] Connected · IP=%s\n", WiFi.localIP().toString().c_str());
}

static void ntp_sync() {
    configTime(TZ_OFFSET_SEC, 0, NTP_SERVER);
    Serial.print("[NTP] Syncing");
    time_t now = 0;
    for (int i = 0; i < 20 && now < 100000; i++) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    if (now > 100000) {
        struct tm* t = localtime(&now);
        Serial.printf("\n[NTP] Time: %02d:%02d:%02d\n", t->tm_hour, t->tm_min, t->tm_sec);
    } else {
        Serial.println("\n[NTP] Sync failed — alarms will be inaccurate");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Arduino entry points
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Totomo Smart Alarm Clock ===");

    pinMode(PIN_BTN, INPUT_PULLUP);

    i2s_mic_init();
    i2s_spk_init();
    opus_init();

    wifi_connect();
    ntp_sync();

    // Build WebSocket URL: /xiaozhi/v1/?device-id=...&client-id=...
    String path = "/xiaozhi/v1/?device-id=";
    path += DEVICE_ID;
    path += "&client-id=";
    path += CLIENT_ID;

    ws.begin(SERVER_HOST, SERVER_PORT, path);
    ws.onEvent(on_ws_event);
    ws.setReconnectInterval(3000);

    Serial.printf("[WS] Connecting to ws://%s:%d%s\n",
                  SERVER_HOST, SERVER_PORT, path.c_str());
}

void loop() {
    ws.loop();            // drives WebSocket (send/receive/reconnect)
    handle_button();      // PTT + long-press abort

    // Stream mic audio every loop tick while PTT is active
    if (state == LISTENING && pttActive) {
        stream_mic_frame();
    }

    // Auto-stop ring after RING_DURATION_MS if button was not pressed
    if (ringing && (millis() - ringStartMs) > RING_DURATION_MS) {
        ringing = false;
        Serial.println("[ALARM] Ring timeout — auto stop");
    }

    // Check alarm every second (only when idle, NTP time valid)
    static unsigned long lastAlarmCheck = 0;
    if (state == IDLE && !ringing && (millis() - lastAlarmCheck) > 1000) {
        lastAlarmCheck = millis();
        check_alarms();
    }
}
