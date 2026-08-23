#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_ir.h  —  IR Remote Hub engine for HermesQ
//
//  Ported from butcher/sketch/sketch.ino (standalone IR app) into
//  a self-contained header that integrates with the HermesQ UI
//  shell the same way hizmos_nfc.h does.
//
//  Entry point: irAppRun()
//    Blocks until the user presses BACK all the way out to the
//    HermesQ main menu.
//
//  Hardware:
//    VS1838B IR receiver : D2   (demodulated active-LOW, direct connect)
//    IR TX-A             : D3   (tone() 38 kHz carrier via STM32 TIM)
//    IR TX-B             : D5   (logic-level copy for second LED)
//    Encoder / buttons already handled by hizmos_input.h.
//
//  Python:
//    ir_main.py registers all ir_* Bridge RPC handlers.
//
//  No external libraries beyond what the main sketch already uses.
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_ui.h"

// ─────────────────────────────────────────────────────────────
//  Pin definitions
// ─────────────────────────────────────────────────────────────
#define IR_RX_PIN   2
#define IR_TX_A     3
#define IR_TX_B     5

// ─────────────────────────────────────────────────────────────
//  UI states (internal to IR app)
// ─────────────────────────────────────────────────────────────
#define IR_ST_IDLE        0
#define IR_ST_REMOTE      1
#define IR_ST_BUTTON      2
#define IR_ST_LEARN_WAIT  3
#define IR_ST_LEARN_DONE  4

// ─────────────────────────────────────────────────────────────
//  Data limits
// ─────────────────────────────────────────────────────────────
#define IR_MAX_REMOTES  20
#define IR_MAX_BUTTONS  30
#define IR_NAME_LEN     18   // max visible chars per list entry

// ─────────────────────────────────────────────────────────────
//  State (all static — never pollutes global scope)
// ─────────────────────────────────────────────────────────────
static volatile uint16_t _irRxPacket[400];
static volatile uint16_t _irRxCount  = 0;
static volatile uint32_t _irLastEdge = 0;
static bool              _irPinsInit = false;

static char    _irRNames[IR_MAX_REMOTES][IR_NAME_LEN];
static uint8_t _irRCnt  = 0;
static int8_t  _irRIdx  = 0;

static char    _irBNames[IR_MAX_BUTTONS][IR_NAME_LEN];
static uint8_t _irBCnt  = 0;
static int8_t  _irBIdx  = 0;

static char _irLearnRemote[IR_NAME_LEN] = "";
static char _irLearnButton[IR_NAME_LEN] = "";

// ─────────────────────────────────────────────────────────────
//  ISR — must be at file scope, kept thin
// ─────────────────────────────────────────────────────────────
void _irRxISR() {
    uint32_t now = micros();
    uint32_t dur = now - _irLastEdge;
    _irLastEdge  = now;
    if (dur > 15000) _irRxCount = 0;           // inter-burst gap → new burst
    if (_irRxCount < 400) _irRxPacket[_irRxCount++] = (uint16_t)dur;
}

// ─────────────────────────────────────────────────────────────
//  Hardware init (called once on first irAppRun())
// ─────────────────────────────────────────────────────────────
static void _irHwInit() {
    if (_irPinsInit) return;
    pinMode(IR_RX_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(IR_RX_PIN), _irRxISR, CHANGE);
    pinMode(IR_TX_A, OUTPUT); digitalWrite(IR_TX_A, LOW);
    pinMode(IR_TX_B, OUTPUT); digitalWrite(IR_TX_B, LOW);
    _irPinsInit = true;
}

// ─────────────────────────────────────────────────────────────
//  Bridge helpers
// ─────────────────────────────────────────────────────────────
static String _irBridgeCall(const char* method, const String& arg = "") {
    String r = "";
    if (arg.length() == 0) Bridge.call(method).result(r);
    else                   Bridge.call(method, arg).result(r);
    return r;
}

// Parse "name1|name2|..." into _irRNames / _irBNames
static void _irParseNames(const String& raw,
                           char names[][IR_NAME_LEN], uint8_t& cnt,
                           uint8_t maxCnt) {
    cnt = 0;
    int pos = 0;
    while (pos < (int)raw.length() && cnt < maxCnt) {
        int pipe = raw.indexOf('|', pos);
        int end  = (pipe == -1) ? raw.length() : pipe;
        String tok = raw.substring(pos, end);
        tok.toCharArray(names[cnt++], IR_NAME_LEN);
        pos = (pipe == -1) ? raw.length() : pipe + 1;
    }
}

static void _irLoadRemotes() {
    String r = _irBridgeCall("ir_list_remotes");
    _irParseNames(r, _irRNames, _irRCnt, IR_MAX_REMOTES);
    if (_irRIdx >= (int8_t)_irRCnt && _irRCnt) _irRIdx = (int8_t)(_irRCnt - 1);
}

static void _irLoadButtons(const char* remote) {
    String r = _irBridgeCall("ir_list_buttons", String(remote));
    _irParseNames(r, _irBNames, _irBCnt, IR_MAX_BUTTONS);
    if (_irBIdx >= (int8_t)_irBCnt && _irBCnt) _irBIdx = (int8_t)(_irBCnt - 1);
}

// ─────────────────────────────────────────────────────────────
//  IR transmit — STM32U585 tone() hardware carrier
// ─────────────────────────────────────────────────────────────
static inline void _irCarrierOn()  {
    tone(IR_TX_A, 38000);
    digitalWrite(IR_TX_B, HIGH);
}
static inline void _irCarrierOff() {
    noTone(IR_TX_A);
    digitalWrite(IR_TX_A, LOW);   // pin may float HIGH after noTone()
    digitalWrite(IR_TX_B, LOW);
}
static void _irEmitRaw(uint16_t* pkt, uint16_t n) {
    if (!n) return;
    bool mark = true;
    for (uint16_t i = 0; i < n; i++) {
        mark ? _irCarrierOn() : _irCarrierOff();
        delayMicroseconds(pkt[i]);
        mark = !mark;
    }
    _irCarrierOff();
    delay(10);
}

// Fetch raw packet from DB and transmit
static void _irEmitButton(const char* remote, const char* button) {
    String key = String(remote) + ":" + String(button);
    String raw = _irBridgeCall("ir_get_packet", key);
    if (!raw.length()) return;

    static uint16_t pkt[400];
    uint16_t n = 0;
    int pos = 0;
    while (pos < (int)raw.length() && n < 400) {
        int pipe = raw.indexOf('|', pos);
        int end  = (pipe == -1) ? raw.length() : pipe;
        pkt[n++] = (uint16_t)raw.substring(pos, end).toInt();
        pos = (pipe == -1) ? raw.length() : pipe + 1;
    }
    _irEmitRaw(pkt, n);
}

// ─────────────────────────────────────────────────────────────
//  DISPLAY HELPERS
// ─────────────────────────────────────────────────────────────
static void _irTitle(const char* t) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, t);
    u8g2.drawHLine(0, 12, 128);
}
static void _irHint(const char* h) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawHLine(0, 55, 128);
    u8g2.drawStr(0, 63, h);
}

// Generic 3-row scrolling list with highlight bar — matches NFC / HermesQ style
static void _irDrawList(const char* title,
                         const char names[][IR_NAME_LEN], uint8_t cnt,
                         int8_t sel, const char* hint) {
    u8g2.clearBuffer();
    _irTitle(title);

    if (cnt == 0) {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(4, 30, "(empty)");
    } else {
        int8_t top = sel - 1;
        if (top < 0)              top = 0;
        if (top + 3 > (int)cnt)  top = (cnt > 3) ? (int8_t)(cnt - 3) : 0;

        u8g2.setFont(u8g2_font_6x10_tr);
        for (uint8_t i = 0; i < 3; i++) {
            int8_t idx = top + (int8_t)i;
            if (idx >= (int8_t)cnt) break;
            int  y   = 26 + (int)i * 13;
            bool cur = (idx == sel);
            if (cur) {
                u8g2.drawRBox(0, y - 10, 120, 12, 2);
                u8g2.setDrawColor(0);
                u8g2.drawStr(4, y, names[idx]);
                u8g2.setDrawColor(1);
            } else {
                u8g2.drawStr(4, y, names[idx]);
            }
        }

        u8g2.setFont(u8g2_font_5x7_tr);
        if (top > 0)             u8g2.drawStr(121, 20, "^");
        if (top + 3 < (int)cnt) u8g2.drawStr(121, 53, "v");
        char sc[8]; snprintf(sc, 8, "%d/%d", sel + 1, cnt);
        u8g2.drawStr(86, 53, sc);
    }

    if (hint && hint[0]) _irHint(hint);
    u8g2.sendBuffer();
}

// Status screen — same style as _nfcStatus
static void _irStatus(const char* title,
                       const char* l1 = "", const char* l2 = "", const char* l3 = "") {
    u8g2.clearBuffer();
    _irTitle(title);
    u8g2.setFont(u8g2_font_6x10_tr);
    if (l1[0]) u8g2.drawStr(0, 26, l1);
    if (l2[0]) u8g2.drawStr(0, 39, l2);
    if (l3[0]) u8g2.drawStr(0, 52, l3);
    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────
//  LEARN — save captured burst to DB, show result
// ─────────────────────────────────────────────────────────────
static void _irSaveLearned() {
    noInterrupts();
    uint16_t n = _irRxCount;
    static uint16_t buf[400];
    for (uint16_t i = 0; i < n; i++) buf[i] = _irRxPacket[i];
    interrupts();

    bool ok = false;

    if (n < 20) {
        _irStatus("LEARN", "Signal too short", "Try again");
    } else {
        // Payload: "remote:button:dur0|dur1|dur2|..."
        String payload = String(_irLearnRemote) + ":" + String(_irLearnButton) + ":";
        for (uint16_t i = 0; i < n; i++) {
            if (i) payload += "|";
            payload += String(buf[i]);
        }
        String r = _irBridgeCall("ir_save_packet", payload);
        ok = (r == "ok");
        if (ok) {
            _irStatus("LEARN", "SAVED!", _irLearnRemote, _irLearnButton);
            _irLoadButtons(_irLearnRemote);
        } else {
            _irStatus("LEARN", "Save failed", r.c_str());
        }
    }
    delay(1500);
}

// ─────────────────────────────────────────────────────────────
//  PUBLIC ENTRY POINT
// ─────────────────────────────────────────────────────────────
static void irAppRun() {
    _irHwInit();
    _irLoadRemotes();
    if (_irRCnt && _irBCnt == 0) _irLoadButtons(_irRNames[_irRIdx]);

    int  irState    = IR_ST_IDLE;
    bool irDirty    = true;
    inputSettle(200);

    // Learn-mode burst tracking
    static uint16_t prevRxCnt = 0;
    static uint32_t lastRxT   = 0;
    static bool     rxSent    = false;

    while (true) {
        inputPoll();

        // ── Render if flagged ─────────────────────────────────
        if (irDirty) {
            irDirty = false;
            switch (irState) {

                case IR_ST_IDLE:
                    u8g2.clearBuffer();
                    _irTitle("IR Remote Hub");
                    u8g2.setFont(u8g2_font_6x10_tr);
                    {
                        char l[32];
                        snprintf(l, 32, "Remote: %.10s", _irRCnt ? _irRNames[_irRIdx] : "---");
                        u8g2.drawStr(0, 26, l);
                        snprintf(l, 32, "Button: %.10s", _irBCnt ? _irBNames[_irBIdx] : "---");
                        u8g2.drawStr(0, 39, l);
                    }
                    _irHint("Rot=menu  Push=emit");
                    u8g2.sendBuffer();
                    break;

                case IR_ST_REMOTE:
                    _irDrawList("Select Remote", _irRNames, _irRCnt, _irRIdx,
                                "Push=open  BACK=exit");
                    break;

                case IR_ST_BUTTON:
                    _irDrawList(_irRNames[_irRIdx], _irBNames, _irBCnt, _irBIdx,
                                _irBCnt ? "Push=emit LEARN=add" : "LEARN=add BACK=up");
                    break;

                case IR_ST_LEARN_WAIT: {
                    u8g2.clearBuffer();
                    _irTitle("LEARN MODE");
                    u8g2.setFont(u8g2_font_6x10_tr);
                    char l[32];
                    snprintf(l, 32, "Remote: %.10s", _irLearnRemote);
                    u8g2.drawStr(0, 26, l);
                    snprintf(l, 32, "Button: %.10s", _irLearnButton);
                    u8g2.drawStr(0, 39, l);
                    u8g2.setFont(u8g2_font_5x7_tr);
                    u8g2.drawStr(6, 51, ">> Aim & press <<");
                    _irHint("BACK=cancel");
                    u8g2.sendBuffer();
                    break;
                }

                default: break;
            }
        }

        // ── State machine ─────────────────────────────────────
        switch (irState) {

            // ── IDLE ──────────────────────────────────────────
            case IR_ST_IDLE: {
                // Any encoder movement enters remote list
                int8_t s = inputConsumeScrollFast(); uiTouchActivity();
                if (s) { irState = IR_ST_REMOTE; irDirty = true; }

                // SELECT button emits last-selected remote/button
                if (inputSelectOrLearnFired() && _irRCnt && _irBCnt) {
                    _irEmitButton(_irRNames[_irRIdx], _irBNames[_irBIdx]);
                }

                // BACK exits IR app back to HermesQ main menu
                if (inputBackFired()) {
                    inputSettle(200);
                    return;
                }
                break;
            }

            // ── REMOTE LIST ───────────────────────────────────
            case IR_ST_REMOTE: {
                int8_t s = inputConsumeScrollFast(); uiTouchActivity();
                if (s && _irRCnt) {
                    _irRIdx = (int8_t)constrain((int)_irRIdx + s, 0, (int)_irRCnt - 1);
                    _irLoadButtons(_irRNames[_irRIdx]);
                    _irBIdx = 0;
                    irDirty = true;
                }
                if (inputSelectOrLearnFired()) {
                    inputSettle(150);
                    _irBIdx  = 0;
                    irState  = IR_ST_BUTTON;
                    irDirty  = true;
                }
                if (inputBackFired()) {
                    inputSettle(150);
                    irState = IR_ST_IDLE;
                    irDirty = true;
                }
                break;
            }

            // ── BUTTON LIST ───────────────────────────────────
            case IR_ST_BUTTON: {
                int8_t s = inputConsumeScrollFast(); uiTouchActivity();
                if (s && _irBCnt) {
                    _irBIdx = (int8_t)constrain((int)_irBIdx + s, 0, (int)_irBCnt - 1);
                    irDirty = true;
                }
                if (inputSelectOrLearnFired() && _irBCnt) {
                    _irEmitButton(_irRNames[_irRIdx], _irBNames[_irBIdx]);
                }
                if (inputLearnFired()) {
                    inputSettle(150);
                    strncpy(_irLearnRemote, _irRNames[_irRIdx], IR_NAME_LEN - 1);
                    _irLearnRemote[IR_NAME_LEN - 1] = '\0';
                    snprintf(_irLearnButton, IR_NAME_LEN, "btn%d", _irBCnt + 1);
                    noInterrupts(); _irRxCount = 0; interrupts();
                    prevRxCnt = 0; rxSent = false;
                    irState   = IR_ST_LEARN_WAIT;
                    irDirty   = true;
                }
                if (inputBackFired()) {
                    inputSettle(150);
                    irState = IR_ST_REMOTE;
                    irDirty = true;
                }
                break;
            }

            // ── LEARN WAIT ────────────────────────────────────
            case IR_ST_LEARN_WAIT: {
                if (_irRxCount != prevRxCnt) {
                    prevRxCnt = _irRxCount;
                    lastRxT   = millis();
                    rxSent    = false;
                }
                // Burst complete when no new edges for 200 ms and we have ≥20 edges
                if (!rxSent && _irRxCount > 20 && millis() - lastRxT > 200) {
                    rxSent = true;
                    _irSaveLearned();
                    irState = IR_ST_BUTTON;
                    irDirty = true;
                }
                if (inputBackFired()) {
                    inputSettle(150);
                    irState = IR_ST_BUTTON;
                    irDirty = true;
                }
                break;
            }

            default:
                irState = IR_ST_IDLE;
                irDirty = true;
                break;
        }

        delay(5);
    }
}
