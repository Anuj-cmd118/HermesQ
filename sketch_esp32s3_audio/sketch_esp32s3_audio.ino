/*
 * HermesQ Audio Co-Processor — Waveshare ESP32-S3 Zero
 * =====================================================
 *
 * WHY THIS BOARD EXISTS
 * ----------------------
 * The Arduino UNO Q does not expose I2S pins, so an I2S microphone and an
 * I2S speaker cannot be wired to it directly. This ESP32-S3 Zero is a
 * dedicated audio front-end: it owns the I2S mic and I2S speaker, and
 * relays raw audio to/from the UNO Q's companion Linux runtime over a
 * plain UART link (NOT the Arduino RouterBridge channel used between the
 * UNO Q's STM32 core and its own Linux side — this is a second, separate
 * UART, S3 <-> UNO Q Linux only).
 *
 * The S3 does no speech-to-text and no intent parsing. Its job is:
 *   1. Capture mic audio (I2S RX) when push-to-talk is held.
 *   2. Stream it to the UNO Q Linux runtime as framed PCM over UART.
 *   3. Play short local tones (I2S TX) on ack / error / listening cues
 *      sent back from the Linux runtime.
 *
 * HARDWARE
 * --------
 *   MCU:      Waveshare ESP32-S3 Zero
 *   Mic:      INMP441 (or similar) I2S MEMS microphone, RX only
 *   Speaker:  MAX98357A (or similar) I2S class-D amp + speaker, TX only
 *   Trigger:  Push-to-talk button (onboard BOOT/GPIO0, or an external
 *             button wired to PTT_PIN) — deliberately push-to-talk
 *             rather than always-on VAD/wake-word: far more reliable in
 *             the field and avoids false triggers from RF/motor noise
 *             near the other HermesQ modules.
 *   Status:   Onboard WS2812 RGB LED (GPIO21 on the Zero) for at-a-glance
 *             state (idle / listening / sending / error) without needing
 *             the OLED.
 *
 * WIRING (adjust the #defines below to match your actual build — these
 * are free GPIOs on the S3 Zero that don't collide with the onboard LED,
 * USB, or the boot-select strapping pins other than the PTT button):
 *
 *   I2S MIC (INMP441)         S3 Zero
 *     BCLK (SCK)        -->   GPIO4
 *     WS   (LRCL)       -->   GPIO5
 *     SD   (DOUT)       -->   GPIO6
 *     L/R                --   GND (mono, left channel)
 *     VDD / GND          --   3V3 / GND
 *
 *   I2S SPEAKER (MAX98357A)   S3 Zero
 *     BCLK               -->  GPIO7
 *     LRC                -->  GPIO15
 *     DIN                -->  GPIO16
 *     GAIN / SD          --   per amp module default
 *     VIN / GND          --   5V (or 3V3) / GND
 *
 *   UART -> UNO Q Linux runtime (separate from RouterBridge)
 *     S3 TX (GPIO17)     -->  UNO Q Linux-side RX
 *     S3 RX (GPIO18)     <--  UNO Q Linux-side TX
 *     GND                --   shared GND with UNO Q
 *
 *   PUSH-TO-TALK
 *     PTT_PIN (default GPIO0 / BOOT, active-LOW, INPUT_PULLUP)
 *
 * PROTOCOL (matches host_daemon/audio_bridge.py, which runs on the UNO Q's
 * Debian host directly — NOT inside the App Lab-managed python/ app, which
 * runs in a container with no access to /dev/tty*. See host_daemon/README.md.)
 * -------------------------------------------------------------
 * Simple framed binary protocol over the UART, no handshake needed:
 *
 *   Byte 0-1  : magic 0xA5 0x5A
 *   Byte 2    : frame type
 *                 0x01 = AUDIO_START  (payload: none)
 *                 0x02 = AUDIO_CHUNK  (payload: raw 16-bit PCM, mono)
 *                 0x03 = AUDIO_END    (payload: none)
 *                 0x10 = TONE_ACK       (Linux -> S3, command understood)
 *                 0x11 = TONE_ERROR     (Linux -> S3, command not understood)
 *                 0x12 = TONE_LISTENING (Linux -> S3, ack that capture started)
 *   Byte 3-4  : payload length, little-endian uint16
 *   Byte 5..N : payload
 *   Byte N+1  : checksum = XOR of all payload bytes (0x00 if length is 0)
 *
 * Audio format streamed to Linux: 16 kHz, 16-bit signed PCM, mono —
 * a safe, common input rate for offline STT engines (e.g. Vosk).
 */

#include <Arduino.h>
#include <driver/i2s.h>
#include <Adafruit_NeoPixel.h>

// ─── Pin configuration ──────────────────────────────────────────────────────
#define MIC_BCLK_PIN     4
#define MIC_WS_PIN       5
#define MIC_SD_PIN       6

#define SPK_BCLK_PIN     7
#define SPK_LRC_PIN      15
#define SPK_DIN_PIN      16

#define UART_TX_PIN      17
#define UART_RX_PIN      18

#define PTT_PIN          0     // BOOT button, active-LOW
#define STATUS_LED_PIN   21
#define STATUS_LED_COUNT 1

// ─── Audio config ───────────────────────────────────────────────────────────
#define SAMPLE_RATE_HZ   16000
#define I2S_MIC_PORT     I2S_NUM_0
#define I2S_SPK_PORT     I2S_NUM_1
#define MIC_DMA_BUF_LEN  512
#define CHUNK_SAMPLES    512     // samples per UART audio chunk
#define MAX_UTTERANCE_MS 8000    // safety cap on a single push-to-talk hold

// ─── Framed UART protocol ───────────────────────────────────────────────────
#define FRAME_MAGIC_0    0xA5
#define FRAME_MAGIC_1    0x5A

enum FrameType : uint8_t {
  FRAME_AUDIO_START   = 0x01,
  FRAME_AUDIO_CHUNK    = 0x02,
  FRAME_AUDIO_END      = 0x03,
  FRAME_TONE_ACK        = 0x10,
  FRAME_TONE_ERROR       = 0x11,
  FRAME_TONE_LISTENING    = 0x12,
};

HardwareSerial LinkUART(1);   // UART1 -> UNO Q Linux runtime
Adafruit_NeoPixel statusLed(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

int16_t micBuf[CHUNK_SAMPLES];

// ─── Status LED helpers ─────────────────────────────────────────────────────
void ledSet(uint8_t r, uint8_t g, uint8_t b) {
  statusLed.setPixelColor(0, statusLed.Color(r, g, b));
  statusLed.show();
}
void ledIdle()      { ledSet(0, 0, 8); }
void ledListening() { ledSet(0, 20, 0); }
void ledSending()   { ledSet(0, 0, 20); }
void ledError()     { ledSet(25, 0, 0); }

// ─── I2S setup ───────────────────────────────────────────────────────────────
void setupMicI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE_HZ,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,   // INMP441 outputs 24-bit in a 32-bit slot
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = MIC_DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = MIC_BCLK_PIN,
    .ws_io_num = MIC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD_PIN
  };
  i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &pins);
  i2s_zero_dma_buffer(I2S_MIC_PORT);
}

void setupSpeakerI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE_HZ,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = MIC_DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = SPK_BCLK_PIN,
    .ws_io_num = SPK_LRC_PIN,
    .data_out_num = SPK_DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_SPK_PORT, &pins);
}

// Reads one chunk of mic audio, converts 32-bit I2S slots -> 16-bit PCM.
size_t readMicChunk(int16_t* out, size_t maxSamples) {
  static int32_t raw[CHUNK_SAMPLES];
  size_t bytesRead = 0;
  i2s_read(I2S_MIC_PORT, raw, maxSamples * sizeof(int32_t), &bytesRead, portMAX_DELAY);
  size_t samples = bytesRead / sizeof(int32_t);
  for (size_t i = 0; i < samples; i++) {
    out[i] = (int16_t)(raw[i] >> 14);   // 32-bit slot, 24 significant bits -> 16-bit PCM
  }
  return samples;
}

// Plays a short local tone directly out the I2S speaker — no dependency on
// the Linux side, so feedback still works even if STT/UART is briefly down.
void playTone(uint16_t freqHz, uint16_t durationMs) {
  const int samples = (SAMPLE_RATE_HZ * durationMs) / 1000;
  int16_t sample;
  size_t written;
  for (int i = 0; i < samples; i++) {
    float t = (float)i / SAMPLE_RATE_HZ;
    sample = (int16_t)(6000.0f * sinf(2.0f * PI * freqHz * t));
    i2s_write(I2S_SPK_PORT, &sample, sizeof(sample), &written, portMAX_DELAY);
    i2s_write(I2S_SPK_PORT, &sample, sizeof(sample), &written, portMAX_DELAY); // stereo-safe dup
  }
}
void toneAck()       { playTone(1200, 90); }
void toneError()     { playTone(300, 220); }
void toneListening() { playTone(900, 60); }

// ─── Framed UART send/receive ────────────────────────────────────────────────
void sendFrame(FrameType type, const uint8_t* payload, uint16_t len) {
  uint8_t checksum = 0;
  for (uint16_t i = 0; i < len; i++) checksum ^= payload[i];
  LinkUART.write(FRAME_MAGIC_0);
  LinkUART.write(FRAME_MAGIC_1);
  LinkUART.write((uint8_t)type);
  LinkUART.write((uint8_t)(len & 0xFF));
  LinkUART.write((uint8_t)((len >> 8) & 0xFF));
  if (len > 0) LinkUART.write(payload, len);
  LinkUART.write(checksum);
}

// Non-blocking parser for frames coming back from the Linux runtime
// (tone cues only — Linux never streams audio back to the S3).
void pollIncomingFrames() {
  static uint8_t state = 0;
  static uint8_t type = 0;
  static uint16_t len = 0, idx = 0;
  static uint8_t payload[64];
  static uint8_t checksum = 0;

  while (LinkUART.available()) {
    uint8_t b = (uint8_t)LinkUART.read();
    switch (state) {
      case 0: state = (b == FRAME_MAGIC_0) ? 1 : 0; break;
      case 1: state = (b == FRAME_MAGIC_1) ? 2 : 0; break;
      case 2: type = b; state = 3; break;
      case 3: len = b; state = 4; break;
      case 4: len |= ((uint16_t)b << 8); idx = 0; checksum = 0;
              state = (len == 0) ? 6 : 5; break;
      case 5:
        if (idx < sizeof(payload)) payload[idx] = b;
        checksum ^= b;
        idx++;
        if (idx >= len) state = 6;
        break;
      case 6: {
        // b == received checksum; ignore mismatch for these tiny cue frames,
        // just act on the type.
        if (type == FRAME_TONE_ACK) { ledSet(0, 20, 0); toneAck(); ledIdle(); }
        else if (type == FRAME_TONE_ERROR) { ledError(); toneError(); ledIdle(); }
        else if (type == FRAME_TONE_LISTENING) { toneListening(); }
        state = 0;
        break;
      }
    }
  }
}

// ─── Push-to-talk capture ────────────────────────────────────────────────────
void doPushToTalk() {
  ledListening();
  sendFrame(FRAME_AUDIO_START, nullptr, 0);

  uint32_t startMs = millis();
  while (digitalRead(PTT_PIN) == LOW && (millis() - startMs) < MAX_UTTERANCE_MS) {
    size_t n = readMicChunk(micBuf, CHUNK_SAMPLES);
    if (n > 0) {
      sendFrame(FRAME_AUDIO_CHUNK, (uint8_t*)micBuf, n * sizeof(int16_t));
    }
  }

  sendFrame(FRAME_AUDIO_END, nullptr, 0);
  ledSending();
  // Result tone (ack/error) arrives asynchronously via pollIncomingFrames()
  // once the Linux side finishes STT + intent parsing + HID execution.
  delay(150);
  ledIdle();
}

// ─── Setup / loop ─────────────────────────────────────────────────────────────
void setup() {
  pinMode(PTT_PIN, INPUT_PULLUP);

  statusLed.begin();
  statusLed.setBrightness(40);
  ledIdle();

  LinkUART.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  setupMicI2S();
  setupSpeakerI2S();

  Serial.begin(115200);
  Serial.println("[HermesQ Audio] ESP32-S3 audio co-processor ready.");
  Serial.println("[HermesQ Audio] Hold PTT (GPIO0 / BOOT) to talk.");
}

void loop() {
  pollIncomingFrames();

  static bool wasHeld = false;
  bool held = (digitalRead(PTT_PIN) == LOW);
  if (held && !wasHeld) {
    doPushToTalk();
  }
  wasHeld = held;

  delay(2);
}
