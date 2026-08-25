// HermesQ UI — Arduino App Lab / Uno Q (STM32U585 + Zephyr)
//
//  Hardware:
//    MCU  : STM32U585 (Cortex-M33 @ 160 MHz)
//    MPU  : Qualcomm QRB2210 (Debian Linux)
//    OLED : SH1106 128×64 I2C  (A4=SDA, A5=SCL)
//    ENC  : ENC_CLK=D6  ENC_DT=D7  ENC_SW=D8  BTN_BACK=D9  BTN_LEARN=D4
//    PN532: SPI on JSPI header — SS=D10  (3.3 V, no level shifting needed)
//    IR RX: VS1838B on D2  (demodulated active-LOW, direct connect)
//    IR TX: D3 (38 kHz carrier via TIM)  D5 (second LED copy)
//    CC1101: SPI shared bus — CSN=A3  GDO0=A2  (remapped to avoid D10/D2)
//
//  Sketch structure (one .ino + header files):
//    sketch.ino            — setup / loop / RPC glue
//    hizmos_arm_compat.h   — Zephyr / ARM compatibility shims
//    hizmos_fonts.h        — font aliases
//    hizmos_display.h      — OLED helpers (splash, loading, drawList)
//    hizmos_input.h        — encoder + button polling
//    hizmos_bridge.h       — Arduino_RouterBridge thin wrappers
//    hizmos_nfc.h          — full NFC engine (PN532 SPI, NDEF, clone, emulate)
//    hizmos_ir.h           — full IR engine  (VS1838B RX, tone() TX, learn/emit)
//    hizmos_feature.h      — feature dispatch (HID, AI, NFC, IR, …)
//    mainmenu.h            — idle animation XBM frames
//    hizmos_mainmenu.h     — animated scrolling main menu renderer
//    hizmos_submenus.h     — per-app sub-menus + handleMenuAction()
//    dolphinreactions.h    — dolphin idle animation frames
//
//  Python side (python/, runs inside App Lab's container):
//    main.py       — Bridge RPC handlers, WebUI REST routes (incl. voice)
//    nfc_main.py   — NFC SQLite DB RPC handlers
//    ir_main.py    — IR SQLite DB RPC handlers
//    rf_main.py    — CC1101 packet log + Bridge notify handlers
//    hid_executor.py, hid_sim.py, ai_agent.py, hermesq_db.py, notes_main.py
//
//  Audio co-processor (sketch_esp32s3_audio/, separate Arduino sketch,
//  flashed to a second board — a Waveshare ESP32-S3 Zero, NOT the UNO Q):
//    The UNO Q does not expose its I2S pins, so an I2S mic (INMP441) and
//    I2S speaker (MAX98357A) cannot be wired to it directly. The S3 Zero
//    owns both over I2S and relays framed audio to/from the UNO Q's Linux
//    host over a dedicated UART (separate from Arduino RouterBridge).
//
//  Host-side voice daemon (host_daemon/, runs on the UNO Q's Debian host,
//  OUTSIDE App Lab's container — see host_daemon/README.md for why):
//    audio_bridge.py — owns the UART link to the S3, offline STT (Vosk),
//    and POSTs transcribed text to this app's POST /api/agent/voice route.
//
//  Libraries required (sketch.yaml):
//    - U8g2
//    - Arduino_RouterBridge
//    - dir: libs/pn532/PN532
//    - dir: libs/pn532/PN532_SPI
//    - dir: libs/pn532/NDEF

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>              // Required by PN532 — must come before U8g2
#include <U8g2lib.h>
#include <Arduino_RouterBridge.h>

#include "hizmos_arm_compat.h"
#include "hizmos_fonts.h"
#include "hizmos_display.h"

#define HIZMOS_INPUT_IMPL
#include "hizmos_input.h"

// NFC engine must be included before feature/submenu headers so that
// nfcAppRun() / nfcAppRead() / nfcAppWrite() / nfcAppSaved() are visible.
#include "hizmos_nfc.h"

// IR engine must be included before feature/submenu headers so that
// irAppRun() is visible.  IR pins (D2/D3/D5) are initialised lazily
// on first irAppRun() call — no conflict with SPI or NFC boot.
#include "hizmos_ir.h"

// RF (CC1101 Sub-GHz) engine.  CS=A3, GDO0=A2 — free pins, no conflicts.
// Hardware init is lazy (first rfAppRun() call).
#include "hizmos_rf.h"

// Standalone apps (no extra hardware dependencies)
#include "hizmos_apps.h"         // Calculator, Pomodoro, Snake, Space Invaders
#include "hizmos_engineering.h"  // 8 engineering calculators
#include "hizmos_settings.h"     // About, Display, SysInfo, Restart (exports g_contrast)
#include "hizmos_notes.h"        // Notes (Browse/New/Search via Bridge)
#include "hizmos_ui.h"           // Toast, Confirm, StatusBar, ScreenSaver, HighScore

#include "mainmenu.h"
#include "hizmos_mainmenu.h"
#include "hizmos_feature.h"
#include "hizmos_submenus.h"

// ─────────────────────────────────────────────────────────────
//  OLED
// ─────────────────────────────────────────────────────────────
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ─────────────────────────────────────────────────────────────
//  UI state machine
// ─────────────────────────────────────────────────────────────
enum UIState  { S_IDLE, S_MENU };
enum JobState { JOB_IDLE, JOB_RUNNING, JOB_DONE };

static UIState  uiState  = S_IDLE;
static bool     dirty    = true;

static uint8_t  idleFrame      = 0;
static uint32_t lastIdleMs     = 0;
static const uint32_t IDLE_MS  = 400;

static bool      jobOverlay  = false;
static String    jobName     = "";
static int       jobProgress = 0;
static JobState  jobState    = JOB_IDLE;

static const uint8_t idleFrameCount =
    sizeof(manualImages) / sizeof(manualImages[0]);

// ─────────────────────────────────────────────────────────────
//  RPC METHODS  (called from Linux side via Bridge)
// ─────────────────────────────────────────────────────────────

bool job_start(String name) {
    jobName = name; jobProgress = 0;
    jobState = JOB_RUNNING; jobOverlay = true;
    return true;
}

bool job_progress(int progress) {
    jobProgress = constrain(progress, 0, 100);
    return true;
}

bool job_done() {
    jobProgress = 100; jobState = JOB_DONE;
    return true;
}

bool job_clear() {
    jobOverlay = false; jobState = JOB_IDLE;
    jobProgress = 0; jobName = "";
    return true;
}

// ─────────────────────────────────────────────────────────────
//  DISPLAY
// ─────────────────────────────────────────────────────────────

static void showIdle() {
    u8g2.clearBuffer();
    u8g2.drawXBMP(0, 0, 128, 64, manualImages[idleFrame % idleFrameCount]);
    u8g2.sendBuffer();
}

static void drawJobOverlay() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.drawStr(4, 12, jobName.c_str());
    u8g2.drawLine(0, 16, 127, 16);

    const int barX = 8, barY = 28, barW = 112, barH = 12;
    u8g2.drawFrame(barX, barY, barW, barH);
    u8g2.drawBox(barX + 1, barY + 1, ((barW - 2) * jobProgress) / 100, barH - 2);

    char percent[16]; snprintf(percent, sizeof(percent), "%d%%", jobProgress);
    u8g2.drawStr(50, 58, percent);
    u8g2.drawStr(4,  58, (jobState == JOB_DONE) ? "Complete" : "Running");
    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────

void setup() {
    Bridge.begin();

    Bridge.provide("job_start",    job_start);
    Bridge.provide("job_progress", job_progress);
    Bridge.provide("job_done",     job_done);
    Bridge.provide("job_clear",    job_clear);

    Wire.begin();
    delay(100);

    // SPI started before nfcInit() is called (lazy on first NFC app open).
    // IR pins are also initialised lazily in irAppRun() — no pin conflict.
    SPI.begin();

    // PN532 CS pin — hold HIGH until NFC app opens
    pinMode(NFC_SS, OUTPUT);
    digitalWrite(NFC_SS, HIGH);

    inputInit();

    u8g2.begin();
    // g_contrast / g_inverted are defined in hizmos_settings.h (included above)
    u8g2.setContrast(g_contrast);
    _uiFullContrast = g_contrast;
    uiTouchActivity();              // seed screen-saver timer
    drawSplash();
    delay(1200);
    drawLoading();
    delay(500);

    inputSettle(400);

    uiState = S_IDLE;
    dirty   = true;

    Serial.begin(115200);
    delay(50);
    Serial.println(F("[HermesQ] Ready."));
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────

void loop() {
    inputPoll();
    uiPollScreenSaver();

    // Any physical input wakes the screen
    if (inputSelectFired() || inputBackFired() || inputLearnFired() ||
        inputScrollPending()) {
        uiTouchActivity();
    }

    // ── Job overlay (progress bar driven from Linux side) ─────
    if (jobOverlay) {
        drawJobOverlay();
        if (inputBackFired()) {
            jobOverlay = false;
            dirty = true;
        }
        delay(5);
        return;
    }

    // ── Main state machine ────────────────────────────────────
    switch (uiState) {

        case S_IDLE:
            inputDiscardScroll();
            if (millis() - lastIdleMs >= IDLE_MS) {
                lastIdleMs = millis();
                idleFrame  = (idleFrame + 1) % idleFrameCount;
                dirty      = true;
            }
            if (inputSelectOrLearnFired()) {
                inputSettle(200);
                uiState = S_MENU;
                dirty   = true;
            }
            if (dirty) {
                dirty = false;
                showIdle();
            }
            break;

        case S_MENU:
            handlemainmenu();
            if (inputBackFired()) {
                inputSettle(200);
                uiState   = S_IDLE;
                dirty     = true;
                idleFrame = 0;
            }
            break;

        default:
            inputDiscardScroll();
            uiState = S_IDLE;
            break;
    }

    delay(5);
}
