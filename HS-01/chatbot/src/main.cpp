/**
 * HS-01 Chatbot  —  ESP32-S3 + INMP441 + MAX98357A
 *
 * Press & hold the button → speak → release → server transcribes your
 * voice (Deepgram nova-2), thinks (DeepSeek), talks back (EdgeTTS Opus)
 * through the speaker.  Works over WiFi — no cloud accounts on the chip.
 *
 * Protocol : xiaozhi WebSocket
 * Server   : totomo-voice.fly.dev  (Fly.io Singapore, always on)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <opus.h>
#include "config.h"

/* ── State machine ──────────────────────────────────────────────── */
enum State { IDLE, LISTENING, PROCESSING, SPEAKING };
static State state = IDLE;

/* ── WebSocket ──────────────────────────────────────────────────── */
static WebSocketsClient ws;
static String  sessionId   = "";
static bool    helloAcked  = false;

/* ── Display helpers ────────────────────────────────────────────── */
static const char* state_label(State s) {
    switch (s) {
        case IDLE:        return "IDLE";
        case LISTENING:   return "🎤 Listening…";
        case PROCESSING:  return "⏳ Thinking…";
        case SPEAKING:    return "🔊 Speaking…";
        default:          return "???";
    }
}

/* ── I2S — INMP441 mic (RX) ────────────────────────────────────── */
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

/* ── I2S — MAX98357A speaker (TX) ──────────────────────────────── */
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

/* ── Opus → PCM → I2S speaker ──────────────────────────────────── */
static OpusDecoder* opusDec = nullptr;
static int16_t      opusPcm[TTS_FRAME_SAMPLES * 2];

static void opus_init() {
    int err;
    opusDec = opus_decoder_create(TTS_SAMPLE_RATE, 1, &err);
    if (err != OPUS_OK)
        Serial.printf("[OPUS] init error %d\n", err);
    else
        Serial.println("[OPUS] ready");
}

static void opus_play(const uint8_t* data, size_t len) {
    if (!opusDec) return;
    int n = opus_decode(opusDec, data, (opus_int32)len,
                        opusPcm, TTS_FRAME_SAMPLES, 0);
    if (n <= 0) { Serial.printf("[OPUS] decode %d\n", n); return; }
    size_t w = 0;
    i2s_write(I2S_NUM_1, opusPcm, n * sizeof(int16_t), &w, portMAX_DELAY);
}

/* ── xiaozhi JSON protocol ─────────────────────────────────────── */
static void ws_emit(JsonDocument& doc) {
    String s;  serializeJson(doc, s);
    ws.sendTXT(s);
}

static void ws_hello() {
    StaticJsonDocument<256> d;
    d["type"]      = "hello";
    d["version"]   = 1;
    d["transport"] = "websocket";
    d["features"]  = JsonObject();
    JsonObject a   = d.createNestedObject("audio_params");
    a["format"]         = "pcm";
    a["sample_rate"]    = MIC_SAMPLE_RATE;
    a["channels"]       = 1;
    a["frame_duration"] = MIC_FRAME_MS;
    ws_emit(d);
}

static void ws_listen(const char* st, const char* mode = "manual") {
    StaticJsonDocument<128> d;
    d["type"]       = "listen";
    d["state"]      = st;
    d["mode"]       = mode;
    d["session_id"] = sessionId;
    ws_emit(d);
}

static void ws_abort() {
    StaticJsonDocument<64> d;
    d["type"]       = "abort";
    d["session_id"] = sessionId;
    ws_emit(d);
}

/* ── WebSocket events ──────────────────────────────────────────── */
static void on_ws(WStype_t t, uint8_t* payload, size_t len) {
    switch (t) {

    case WStype_CONNECTED:
        helloAcked = false;
        Serial.println("[WS] connected → hello");
        ws_hello();
        break;

    case WStype_DISCONNECTED:
        helloAcked = false;
        sessionId  = "";
        state      = IDLE;
        Serial.println("[WS] closed  (will auto-reconnect)");
        break;

    case WStype_TEXT: {
        StaticJsonDocument<1024> d;
        if (deserializeJson(d, payload, len) != DeserializationError::Ok)
            break;
        const char* mt = d["type"] | "";

        if (!strcmp(mt, "hello")) {
            sessionId  = d["session_id"] | "";
            helloAcked = true;
            Serial.printf("[WS] ready · session=%s\n", sessionId.c_str());
            state = IDLE;

        } else if (!strcmp(mt, "stt")) {
            const char* txt = d["text"] | "";
            if (txt[0] && txt[0] != '%') {
                Serial.printf("[STT] \"%s\"\n", txt);
                state = PROCESSING;
            }

        } else if (!strcmp(mt, "llm")) {
            Serial.printf("[😊]   %s\n", d["text"] | "");

        } else if (!strcmp(mt, "tts")) {
            const char* s = d["state"] | "";
            if (!strcmp(s, "start")) {
                state = SPEAKING;
                Serial.println("[TTS] ▸");
            } else if (!strcmp(s, "sentence_start"))
                Serial.printf("[TTS]   “%s”\n", d["text"] | "");
            else if (!strcmp(s, "stop")) {
                state = IDLE;
                Serial.println("[TTS] ◂  (done)");
            }
        }
        break;
    }

    case WStype_BIN:
        if (state == SPEAKING) opus_play(payload, len);
        break;

    default: break;
    }
}

/* ── Button ────────────────────────────────────────────────────── */
static bool         pttActive    = false;
static bool         btnPrev      = HIGH;
static unsigned long btnPressAt  = 0;

static void handle_button() {
    bool cur = digitalRead(PIN_BTN);

    if (btnPrev == HIGH && cur == LOW) {      // press
        btnPressAt = millis();
        btnPrev    = LOW;
    }

    if (btnPrev == LOW && cur == HIGH) {      // release
        unsigned long held = millis() - btnPressAt;
        btnPrev = HIGH;

        if (held >= BTN_LONG_PRESS_MS) {      // long press = abort
            if (state != IDLE) { ws_abort(); state = IDLE; }
            Serial.println("[BTN] abort");
        } else if (pttActive) {               // normal release → stop mic
            pttActive = false;
            ws_listen("stop");
            state = PROCESSING;
            Serial.printf("[BTN] stop · %lu ms\n", held);
        }
    }

    // while held → start mic if ready
    if (cur == LOW && !pttActive && helloAcked && state == IDLE) {
        if ((millis() - btnPressAt) > 50) {
            pttActive = true;
            state     = LISTENING;
            ws_listen("start");
            Serial.println("[BTN] 🎤 start");
        }
    }
    btnPrev = cur;
}

/* ── Mic streaming ──────────────────────────────────────────────── */
static int32_t micRaw[MIC_FRAME_SAMPLES];
static int16_t micPcm[MIC_FRAME_SAMPLES];

static void stream_mic() {
    size_t nr = 0;
    if (i2s_read(I2S_NUM_0, micRaw, sizeof(micRaw), &nr, 0) != ESP_OK || nr == 0)
        return;
    int n = nr / 4;
    for (int i = 0; i < n; i++)
        micPcm[i] = (int16_t)(micRaw[i] >> 11);
    ws.sendBIN((const uint8_t*)micPcm, n * sizeof(int16_t));
}

/* ── WiFi ──────────────────────────────────────────────────────── */
static void wifi_connect() {
    Serial.printf("[WiFi] %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("  ✓  %s\n", WiFi.localIP().toString().c_str());
}

/* ── Entry ─────────────────────────────────────────────────────── */
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n════════════════════════════");
    Serial.println("  HS-01 Chatbot");
    Serial.println("  server  totomo-voice.fly.dev:80");
    Serial.println("════════════════════════════\n");

    pinMode(PIN_BTN, INPUT_PULLUP);
    i2s_mic_init();
    i2s_spk_init();
    opus_init();
    wifi_connect();

    String path = "/xiaozhi/v1/?device-id=";
    path += DEVICE_ID;
    path += "&client-id=";
    path += CLIENT_ID;

    ws.begin(SERVER_HOST, SERVER_PORT, path);
    ws.onEvent(on_ws);
    ws.setReconnectInterval(3000);

    Serial.printf("[WS]  ws://%s:%d%s\n", SERVER_HOST, SERVER_PORT, path.c_str());
    Serial.println("      Hold button to talk.\n");
}

void loop() {
    ws.loop();
    handle_button();

    if (state == LISTENING && pttActive)
        stream_mic();

    // Log state changes to serial
    static State lastState = IDLE;
    if (state != lastState) {
        Serial.printf("[→]   %s\n", state_label(state));
        lastState = state;
    }
}
