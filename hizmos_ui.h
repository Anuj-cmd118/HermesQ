#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_ui.h  —  Shared UI primitives for HermesQ
//
//  Provides:
//    uiToast(msg, ms)          — overlay message for N ms
//    uiConfirm(msg)            — Yes/No dialog, returns bool
//    uiStatusBar()             — 128×10 top bar (uptime, BLE icon)
//    uiScreenDim()             — dim OLED after timeout
//    uiDrawHint(h)             — standardised bottom hint bar
//    uiDrawTitle(t)            — standardised top title + divider
//    uiStepMenu(val,steps,n)   — inline step-size picker overlay
//    uiHighScore(key,score)    — simple RAM high-score store/read
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <U8g2lib.h>
#include "hizmos_input.h"

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

// ─────────────────────────────────────────────────────────────
//  Screen-saver / dim state (driven from main loop)
// ─────────────────────────────────────────────────────────────
static uint32_t _uiLastActivityMs  = 0;
static bool     _uiDimmed          = false;
static uint8_t  _uiFullContrast    = 200;   // set by settings on boot
static const uint32_t UI_DIM_MS    = 30000; // 30 s

inline void uiTouchActivity() {
    if (_uiDimmed) {
        u8g2.setContrast(_uiFullContrast);
        _uiDimmed = false;
    }
    _uiLastActivityMs = millis();
}

// Call once per loop() tick
inline void uiPollScreenSaver() {
    if (!_uiDimmed && millis() - _uiLastActivityMs >= UI_DIM_MS) {
        u8g2.setContrast(4);   // nearly off but not blank
        _uiDimmed = true;
    }
}

// ─────────────────────────────────────────────────────────────
//  Standard title + hint bars
// ─────────────────────────────────────────────────────────────
inline void uiDrawTitle(const char* t) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, t);
    u8g2.drawHLine(0, 12, 128);
}

inline void uiDrawHint(const char* h) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawHLine(0, 55, 128);
    u8g2.drawStr(0, 63, h);
}

// ─────────────────────────────────────────────────────────────
//  Status bar — shown in idle screen top strip
// ─────────────────────────────────────────────────────────────
inline void uiDrawStatusBar(bool bleConnected) {
    u8g2.setFont(u8g2_font_5x7_tr);
    // Uptime h:mm
    uint32_t s = millis() / 1000;
    char buf[12]; snprintf(buf, 12, "%luh%02lum", (unsigned long)(s/3600), (unsigned long)((s/60)%60));
    u8g2.drawStr(0, 7, buf);
    // BLE indicator
    if (bleConnected) {
        // simple B glyph
        u8g2.drawStr(112, 7, "BLE");
    }
    u8g2.drawHLine(0, 9, 128);
}

// ─────────────────────────────────────────────────────────────
//  Toast — non-blocking overlay drawn over current buffer
//  Blocks for `durationMs` ms while still polling input.
// ─────────────────────────────────────────────────────────────
inline void uiToast(const char* msg, uint16_t durationMs = 1200) {
    uiTouchActivity();
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        // Draw toast on top of whatever is on-screen
        // We redraw the toast each tick so it composites cleanly
        int w = (int)strlen(msg) * 6 + 8;
        if (w > 120) w = 120;
        int x = (128 - w) / 2;
        u8g2.setDrawColor(0);
        u8g2.drawBox(x - 1, 26, w + 2, 14);
        u8g2.setDrawColor(1);
        u8g2.drawFrame(x - 1, 26, w + 2, 14);
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(x + 3, 36, msg);
        u8g2.sendBuffer();
        delay(16);
    }
}

// ─────────────────────────────────────────────────────────────
//  Confirm dialog — Yes/No, returns true for Yes
// ─────────────────────────────────────────────────────────────
inline bool uiConfirm(const char* msg) {
    int8_t  choice = 0;  // 0=No, 1=Yes
    bool    dirty  = true;
    inputSettle(150);

    while (true) {
        inputPoll();
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer();
            uiDrawTitle("Confirm");
            u8g2.setFont(u8g2_font_6x10_tr);
            // Word-wrap msg at 20 chars
            char line1[22] = "", line2[22] = "";
            size_t len = strlen(msg);
            if (len <= 20) {
                strncpy(line1, msg, 21);
            } else {
                strncpy(line1, msg, 20); line1[20] = '\0';
                strncpy(line2, msg + 20, 21);
            }
            u8g2.drawStr(2, 26, line1);
            if (line2[0]) u8g2.drawStr(2, 37, line2);

            // No button
            if (choice == 0) { u8g2.drawRBox(4,  42, 44, 12, 3); u8g2.setDrawColor(0); }
            u8g2.drawStr(12, 51, "No");
            if (choice == 0) u8g2.setDrawColor(1);

            // Yes button
            if (choice == 1) { u8g2.drawRBox(78, 42, 46, 12, 3); u8g2.setDrawColor(0); }
            u8g2.drawStr(86, 51, "Yes");
            if (choice == 1) u8g2.setDrawColor(1);

            uiDrawHint("Rot=pick  Sel=confirm");
            u8g2.sendBuffer();
        }

        int8_t s = inputConsumeScroll();
        if (s) { choice = (choice == 0) ? 1 : 0; dirty = true; }
        if (inputSelectOrLearnFired()) { inputSettle(150); return choice == 1; }
        if (inputBackFired())          { inputSettle(150); return false; }
        delay(5);
    }
}

// ─────────────────────────────────────────────────────────────
//  Step-size picker overlay — shown when LEARN held in field editors
//  steps[] is an array of floats, n is count, returns chosen step
// ─────────────────────────────────────────────────────────────
inline float uiPickStep(const float* steps, uint8_t n, float current) {
    int8_t sel = 0;
    for (uint8_t i = 0; i < n; i++) if (steps[i] == current) { sel = (int8_t)i; break; }
    bool dirty = true;
    inputSettle(150);

    while (true) {
        inputPoll();
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer();
            uiDrawTitle("Step size");
            u8g2.setFont(u8g2_font_6x10_tr);
            for (uint8_t i = 0; i < n && i < 4; i++) {
                int y = 26 + (int)i * 10;
                bool s = (i == (uint8_t)sel);
                char buf[12];
                if (steps[i] < 1.0f)      snprintf(buf, 12, "%.3f", steps[i]);
                else if (steps[i] < 10.0f) snprintf(buf, 12, "%.1f", steps[i]);
                else                        snprintf(buf, 12, "%.0f", steps[i]);
                if (s) { u8g2.drawRBox(0, y - 8, 80, 10, 2); u8g2.setDrawColor(0); }
                u8g2.drawStr(4, y, buf);
                if (s) u8g2.setDrawColor(1);
            }
            uiDrawHint("Rot=pick  Sel=ok");
            u8g2.sendBuffer();
        }
        int8_t sc = inputConsumeScroll();
        if (sc) { sel = (int8_t)constrain((int)sel + sc, 0, (int)n - 1); dirty = true; }
        if (inputSelectOrLearnFired()) { inputSettle(100); return steps[sel]; }
        if (inputBackFired())          { inputSettle(100); return current; }
        delay(5);
    }
}

// ─────────────────────────────────────────────────────────────
//  High-score store — simple keyed RAM table
// ─────────────────────────────────────────────────────────────
#define UI_HS_MAX 4
struct _UiHsEntry { char key[12]; uint16_t score; };
static _UiHsEntry _uiHsTable[UI_HS_MAX];
static uint8_t    _uiHsCnt = 0;

inline uint16_t uiHighScoreGet(const char* key) {
    for (uint8_t i = 0; i < _uiHsCnt; i++)
        if (strncmp(_uiHsTable[i].key, key, 11) == 0) return _uiHsTable[i].score;
    return 0;
}
inline void uiHighScoreSet(const char* key, uint16_t score) {
    for (uint8_t i = 0; i < _uiHsCnt; i++) {
        if (strncmp(_uiHsTable[i].key, key, 11) == 0) {
            if (score > _uiHsTable[i].score) _uiHsTable[i].score = score;
            return;
        }
    }
    if (_uiHsCnt < UI_HS_MAX) {
        strncpy(_uiHsTable[_uiHsCnt].key, key, 11);
        _uiHsTable[_uiHsCnt].score = score;
        _uiHsCnt++;
    }
}
