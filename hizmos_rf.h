#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_rf.h  —  CC1101 Sub-GHz engine for HermesQ
//
//  Ported from cc1101_rf_monitor/sketch/sketch.ino and adapted
//  to the HermesQ UI shell (header-only, static state, same
//  patterns as hizmos_nfc.h / hizmos_ir.h).
//
//  Entry point: rfAppRun()
//    Blocks until the user presses BACK all the way out to the
//    HermesQ main menu.
//
//  Hardware — CC1101 on shared SPI bus:
//    MOSI=D11  MISO=D12  SCK=D13  (shared with PN532)
//    CSN = A3  ← remapped from D10 (D10 = NFC_SS, taken)
//    GDO0= A2  ← remapped from D2  (D2  = IR_RX,  taken)
//
//  Encoder / buttons: uses HermesQ's shared hizmos_input.h.
//    Rotate   → tune frequency
//    SELECT   → toggle RX/TX mode  (or transmit test packet in TX mode)
//    LEARN    → transmit test packet in TX mode
//    BACK     → exit to main menu
//
//  Python: rf_main.py registers all rf_* Bridge RPC handlers.
//
//  Library required (add to sketch.yaml):
//    - ELECHOUSE_CC1101_SRC_DRV
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <SPI.h>
#include "ELECHOUSE_CC1101_SRC_DRV.h"
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_ui.h"

// ─────────────────────────────────────────────────────────────
//  Pin definitions (conflict-free with NFC and IR)
// ─────────────────────────────────────────────────────────────
#define RF_CS_PIN    A3   // CC1101 CSN  — A3 free (D10 = NFC_SS)
#define RF_GDO0_PIN  A2   // CC1101 GDO0 — A2 free (D2  = IR_RX)

// ─────────────────────────────────────────────────────────────
//  UI states
// ─────────────────────────────────────────────────────────────
#define RF_ST_MENU     0
#define RF_ST_MONITOR  1   // live RX packet log
#define RF_ST_TX       2   // TX ready screen
#define RF_ST_SCAN     3   // frequency scan (RSSI sweep)
#define RF_ST_SETTINGS 4

// ─────────────────────────────────────────────────────────────
//  Static state
// ─────────────────────────────────────────────────────────────
static volatile bool  _rfRxFlag   = false;
static bool           _rfPinsInit = false;
static bool           _rfReady    = false;
static bool           _rfRxMode   = true;

static float          _rfFreq     = 433.92f;  // current frequency MHz
static int            _rfFreqStep = 0;         // offset in 0.1 MHz steps (0-90 → 433.00-441.90)
static const float    RF_FREQ_BASE = 433.00f;

// Last received packet fields (for OLED display)
static char   _rfRxData[65]  = "";
static int    _rfRssi        = 0;
static int    _rfLqi         = 0;
static bool   _rfCrcOk       = false;
static uint8_t _rfRxLen      = 0;
static bool   _rfNewPacket   = false;

// Packet log — ring buffer, newest at index 0
#define RF_LOG_MAX  8
struct RfLogEntry {
    char    data[24];
    int8_t  rssi;
    bool    crcOk;
};
static RfLogEntry _rfLog[RF_LOG_MAX];
static uint8_t    _rfLogCnt = 0;

// Sub-GHz menu
#define RF_MENU_CNT 4
static const char* _rfMenuItems[RF_MENU_CNT] = {
    "Monitor", "Transmit", "Freq Scan", "Settings"
};
static int _rfMenuCur = 0, _rfMenuScroll = 0;

// Settings
static float  _rfSettingsFreqs[] = { 315.00f, 433.92f, 868.35f, 915.00f };
static const char* _rfFreqLabels[] = { "315.00 MHz", "433.92 MHz", "868.35 MHz", "915.00 MHz" };
#define RF_PRESET_CNT 4
static int    _rfPresetCur = 1;  // default 433.92

// Scan state
#define RF_SCAN_STEPS 10
static int8_t _rfScanRssi[RF_SCAN_STEPS];  // RSSI per 0.1 MHz step
static int    _rfScanStep  = 0;
static bool   _rfScanning  = false;

// ─────────────────────────────────────────────────────────────
//  ISR
// ─────────────────────────────────────────────────────────────
void _rfGdo0ISR() { _rfRxFlag = true; }

// ─────────────────────────────────────────────────────────────
//  Hardware init (lazy — once per power cycle)
// ─────────────────────────────────────────────────────────────
static bool _rfHwInit() {
    if (_rfPinsInit) return _rfReady;

    _rfPinsInit = true;

    ELECHOUSE_cc1101.setSpiPin(13, 12, 11, RF_CS_PIN);
    ELECHOUSE_cc1101.Init();

    if (!ELECHOUSE_cc1101.getCC1101()) {
        _rfReady = false;
        return false;
    }

    ELECHOUSE_cc1101.setMHZ(_rfFreq);
    ELECHOUSE_cc1101.setPA(10);
    ELECHOUSE_cc1101.setDRate(4.8);
    ELECHOUSE_cc1101.setModulation(0);  // 2-FSK
    ELECHOUSE_cc1101.setCrc(1);

    pinMode(RF_GDO0_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(RF_GDO0_PIN), _rfGdo0ISR, RISING);

    ELECHOUSE_cc1101.SetRx();
    _rfRxMode = true;
    _rfReady  = true;
    return true;
}

// ─────────────────────────────────────────────────────────────
//  Radio helpers
// ─────────────────────────────────────────────────────────────
static void _rfApplyFreq(float mhz) {
    _rfFreq = mhz;
    ELECHOUSE_cc1101.setMHZ(_rfFreq);
    // Notify Python side so it can log
    String msg = String("{\"freq\":") + String(_rfFreq, 2) + "}";
    Bridge.notify("rf_freq_update", msg.c_str());
}

static void _rfSetRx() {
    _rfRxMode = true;
    ELECHOUSE_cc1101.SetRx();
    Bridge.notify("rf_mode_change", "{\"mode\":\"RX\"}");
}

static void _rfSetTx() {
    _rfRxMode = false;
    Bridge.notify("rf_mode_change", "{\"mode\":\"TX\"}");
}

static void _rfTransmit(const char* payload) {
    ELECHOUSE_cc1101.SendData((byte*)payload, strlen(payload));
    String msg = String("{\"data\":\"") + payload
               + "\",\"freq\":" + String(_rfFreq, 2) + "}";
    Bridge.notify("rf_tx_sent", msg.c_str());
    _rfSetRx();  // return to RX after TX
}

// Poll GDO0 flag and update _rfRxData etc.
static void _rfPollRx() {
    if (!_rfRxFlag || !_rfRxMode) return;
    _rfRxFlag = false;

    static byte buf[64];
    _rfRxLen = ELECHOUSE_cc1101.ReceiveData(buf);
    if (_rfRxLen > 63) _rfRxLen = 63;
    buf[_rfRxLen] = '\0';
    memcpy(_rfRxData, buf, _rfRxLen + 1);

    _rfRssi   = ELECHOUSE_cc1101.getRssi();
    _rfLqi    = ELECHOUSE_cc1101.getLqi();
    _rfCrcOk  = ELECHOUSE_cc1101.CheckCRC();

    // Add to log ring
    RfLogEntry e;
    strncpy(e.data, _rfRxData, 23); e.data[23] = '\0';
    e.rssi  = (int8_t)_rfRssi;
    e.crcOk = _rfCrcOk;
    if (_rfLogCnt < RF_LOG_MAX) {
        // shift up
        for (int i = _rfLogCnt; i > 0; i--) _rfLog[i] = _rfLog[i - 1];
        _rfLog[0] = e;
        _rfLogCnt++;
    } else {
        for (int i = RF_LOG_MAX - 1; i > 0; i--) _rfLog[i] = _rfLog[i - 1];
        _rfLog[0] = e;
    }

    // Notify Python (it stores full JSON log)
    String payload = String("{\"data\":\"") + _rfRxData
        + "\",\"rssi\":" + _rfRssi
        + ",\"lqi\":"   + _rfLqi
        + ",\"crc\":"   + (_rfCrcOk ? 1 : 0)
        + ",\"len\":"   + _rfRxLen
        + ",\"freq\":"  + String(_rfFreq, 2) + "}";
    Bridge.notify("rf_packet", payload.c_str());

    _rfNewPacket = true;
    ELECHOUSE_cc1101.SetRx();
}

// ─────────────────────────────────────────────────────────────
//  DISPLAY HELPERS
// ─────────────────────────────────────────────────────────────
static void _rfTitle(const char* t) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, t);
    u8g2.drawHLine(0, 12, 128);
}
static void _rfHint(const char* h) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawHLine(0, 55, 128);
    u8g2.drawStr(0, 63, h);
}
static void _rfStatus(const char* title,
                       const char* l1 = "", const char* l2 = "", const char* l3 = "") {
    u8g2.clearBuffer();
    _rfTitle(title);
    u8g2.setFont(u8g2_font_6x10_tr);
    if (l1[0]) u8g2.drawStr(0, 26, l1);
    if (l2[0]) u8g2.drawStr(0, 39, l2);
    if (l3[0]) u8g2.drawStr(0, 52, l3);
    u8g2.sendBuffer();
}
static void _rfDrawMenu() {
    u8g2.clearBuffer();
    _rfTitle("Sub-GHz");
    u8g2.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < 3 && (_rfMenuScroll + i) < RF_MENU_CNT; i++) {
        int idx = _rfMenuScroll + i;
        int y   = 26 + i * 13;
        bool sel = (idx == _rfMenuCur);
        if (sel) {
            u8g2.drawRBox(0, y - 10, 124, 12, 2);
            u8g2.setDrawColor(0);
            u8g2.drawStr(4, y, _rfMenuItems[idx]);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(4, y, _rfMenuItems[idx]);
        }
    }
    u8g2.setFont(u8g2_font_5x7_tr);
    if (_rfMenuScroll > 0)                       u8g2.drawStr(120, 22, "^");
    if (_rfMenuScroll + 3 < RF_MENU_CNT)         u8g2.drawStr(120, 63, "v");
    char sc[8]; snprintf(sc, 8, "%d/%d", _rfMenuCur + 1, RF_MENU_CNT);
    u8g2.drawStr(86, 63, sc);
    u8g2.sendBuffer();
}

// Monitor screen — shows freq/mode banner + last 3 log entries
static void _rfDrawMonitor() {
    u8g2.clearBuffer();
    _rfTitle("Sub-GHz Monitor");

    u8g2.setFont(u8g2_font_5x7_tr);
    char hdr[32];
    snprintf(hdr, 32, "%s %.2fMHz", _rfRxMode ? "RX" : "TX", _rfFreq);
    u8g2.drawStr(0, 22, hdr);

    if (_rfLogCnt == 0) {
        u8g2.drawStr(0, 34, "Waiting for packets...");
    } else {
        for (int i = 0; i < 3 && i < (int)_rfLogCnt; i++) {
            char row[28];
            snprintf(row, 28, "%-16s %ddB", _rfLog[i].data, _rfLog[i].rssi);
            u8g2.drawStr(0, 34 + i * 9, row);
        }
    }

    _rfHint("Rot=tune Sel=TX BACK=menu");
    u8g2.sendBuffer();
}

// TX screen
static void _rfDrawTx() {
    u8g2.clearBuffer();
    _rfTitle("Sub-GHz TX");
    u8g2.setFont(u8g2_font_6x10_tr);
    char l[32];
    snprintf(l, 32, "Freq: %.2f MHz", _rfFreq);
    u8g2.drawStr(0, 26, l);
    u8g2.drawStr(0, 39, "Payload: HermesQ-TX");
    _rfHint("Sel/Lrn=emit  BACK=menu");
    u8g2.sendBuffer();
}

// Frequency scan bar chart — 10 steps × 1 bar each
static void _rfDrawScan() {
    u8g2.clearBuffer();
    _rfTitle("Freq Scan");
    u8g2.setFont(u8g2_font_5x7_tr);

    // X: 10 bars across 128px, each 11px wide with 2px gap
    for (int i = 0; i < RF_SCAN_STEPS; i++) {
        int x   = 4 + i * 12;
        int8_t r = _rfScanRssi[i];          // negative dBm
        // Map rssi -100..-30 → height 1..32
        int h = (r == 0) ? 1 : constrain(map(-r, 30, 100, 32, 1), 1, 32);
        u8g2.drawBox(x, 54 - h, 10, h);
    }
    u8g2.drawHLine(0, 55, 128);

    if (_rfScanning) {
        char sl[28];
        snprintf(sl, 28, "Scanning %.2fMHz...", RF_FREQ_BASE + _rfScanStep * 0.1f);
        u8g2.drawStr(0, 63, sl);
    } else {
        u8g2.drawStr(0, 13, "433.00");
        u8g2.drawStr(90, 13, "433.90");
        _rfHint("Sel=rescan  BACK=menu");
    }
    u8g2.sendBuffer();
}

// Settings screen — frequency presets
static void _rfDrawSettings() {
    u8g2.clearBuffer();
    _rfTitle("RF Settings");
    u8g2.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < 3 && (_rfPresetCur - 1 + i) < RF_PRESET_CNT; i++) {
        int idx = _rfPresetCur - 1 + i;
        if (idx < 0 || idx >= RF_PRESET_CNT) continue;
        int y   = 26 + i * 13;
        bool sel = (idx == _rfPresetCur);
        if (sel) {
            u8g2.drawRBox(0, y - 10, 124, 12, 2);
            u8g2.setDrawColor(0);
            u8g2.drawStr(4, y, _rfFreqLabels[idx]);
            u8g2.setDrawColor(1);
        } else {
            u8g2.drawStr(4, y, _rfFreqLabels[idx]);
        }
    }
    _rfHint("Sel=apply  BACK=menu");
    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────
//  SUB-SCREEN RUNNERS
// ─────────────────────────────────────────────────────────────

static void _rfRunMonitor() {
    bool dirty = true;
    _rfNewPacket = false;
    inputSettle(150);

    while (true) {
        inputPoll();
        _rfPollRx();

        if (_rfNewPacket) { _rfNewPacket = false; dirty = true; }
        if (dirty) { dirty = false; _rfDrawMonitor(); }

        // Rotate → tune frequency in 0.1 MHz steps
        int8_t s = inputConsumeScrollFast(); uiTouchActivity();
        if (s) {
            _rfFreqStep = constrain(_rfFreqStep + (int)s, 0, 90);
            _rfApplyFreq(RF_FREQ_BASE + _rfFreqStep * 0.1f);
            dirty = true;
        }

        // SELECT → switch to TX mode (go to TX screen)
        if (inputSelectFired()) {
            inputSettle(150);
            _rfSetTx();
            return;   // caller will open TX screen
        }

        if (inputBackFired()) {
            inputSettle(150);
            _rfSetRx();   // ensure RX when returning to menu
            return;
        }

        delay(5);
    }
}

static void _rfRunTx() {
    bool dirty = true;
    inputSettle(150);

    while (true) {
        inputPoll();

        if (dirty) { dirty = false; _rfDrawTx(); }

        // Rotate → fine-tune frequency
        int8_t s = inputConsumeScrollFast(); uiTouchActivity();
        if (s) {
            _rfFreqStep = constrain(_rfFreqStep + (int)s, 0, 90);
            _rfApplyFreq(RF_FREQ_BASE + _rfFreqStep * 0.1f);
            dirty = true;
        }

        // SELECT or LEARN → transmit test packet
        if (inputSelectOrLearnFired()) {
            _rfTransmit("HermesQ-TX");
            _rfStatus("TX", "Packet sent!", String("Freq:" + String(_rfFreq, 2)).c_str());
            delay(1000);
            dirty = true;
        }

        if (inputBackFired()) {
            inputSettle(150);
            _rfSetRx();
            return;
        }

        delay(5);
    }
}

static void _rfRunScan() {
    // Sweep 10 steps, sample RSSI at each, then show bar chart
    _rfScanning = true;
    memset(_rfScanRssi, 0, sizeof(_rfScanRssi));

    for (_rfScanStep = 0; _rfScanStep < RF_SCAN_STEPS; _rfScanStep++) {
        float f = RF_FREQ_BASE + _rfScanStep * 0.1f;
        ELECHOUSE_cc1101.setMHZ(f);
        ELECHOUSE_cc1101.SetRx();
        delay(80);   // settle + sample
        _rfScanRssi[_rfScanStep] = (int8_t)ELECHOUSE_cc1101.getRssi();
        _rfDrawScan();
    }

    // Restore original frequency + RX
    ELECHOUSE_cc1101.setMHZ(_rfFreq);
    ELECHOUSE_cc1101.SetRx();
    _rfScanning = false;
    _rfDrawScan();
    inputSettle(150);

    // Wait for SELECT (rescan) or BACK (exit)
    while (true) {
        inputPoll();

        if (inputSelectOrLearnFired()) {
            inputSettle(150);
            _rfRunScan();  // recurse for another sweep
            return;
        }
        if (inputBackFired()) {
            inputSettle(150);
            return;
        }
        delay(5);
    }
}

static void _rfRunSettings() {
    bool dirty = true;
    inputSettle(150);

    while (true) {
        inputPoll();
        if (dirty) { dirty = false; _rfDrawSettings(); }

        int8_t s = inputConsumeScrollFast(); uiTouchActivity();
        if (s) {
            _rfPresetCur = constrain(_rfPresetCur + (int)s, 0, RF_PRESET_CNT - 1);
            dirty = true;
        }

        if (inputSelectOrLearnFired()) {
            inputSettle(150);
            _rfApplyFreq(_rfSettingsFreqs[_rfPresetCur]);
            // Update step offset so monitor tuning stays in sync
            _rfFreqStep = (int)((_rfFreq - RF_FREQ_BASE) / 0.1f + 0.5f);
            if (_rfFreqStep < 0 || _rfFreqStep > 90) _rfFreqStep = 0;
            _rfStatus("RF Settings", "Frequency set:", _rfFreqLabels[_rfPresetCur]);
            delay(1200);
            dirty = true;
        }

        if (inputBackFired()) {
            inputSettle(150);
            return;
        }

        delay(5);
    }
}

// ─────────────────────────────────────────────────────────────
//  PUBLIC ENTRY POINT
// ─────────────────────────────────────────────────────────────
static void rfAppRun() {
    if (!_rfHwInit()) {
        _rfStatus("Sub-GHz Error", "CC1101 not found",
                  "Check SPI / A3", "BACK to exit");
        while (true) {
            inputPoll();
            if (inputBackFired()) { inputSettle(150); return; }
            delay(5);
        }
    }

    _rfMenuCur = 0; _rfMenuScroll = 0;
    int rfState = RF_ST_MENU;
    bool dirty  = true;
    inputSettle(200);

    while (true) {
        inputPoll();

        if (rfState == RF_ST_MENU) {
            if (dirty) { dirty = false; _rfDrawMenu(); }

            int8_t s = inputConsumeScrollFast(); uiTouchActivity();
            if (s) {
                _rfMenuCur = constrain(_rfMenuCur + (int)s, 0, RF_MENU_CNT - 1);
                if (_rfMenuCur < _rfMenuScroll)      _rfMenuScroll = _rfMenuCur;
                if (_rfMenuCur >= _rfMenuScroll + 3) _rfMenuScroll = _rfMenuCur - 2;
                dirty = true;
            }

            if (inputSelectOrLearnFired()) {
                inputSettle(150);
                switch (_rfMenuCur) {
                    case 0:
                        _rfSetRx();
                        _rfRunMonitor();
                        // If monitor exits via SELECT (→TX), open TX screen
                        if (!_rfRxMode) _rfRunTx();
                        break;
                    case 1:
                        _rfSetTx();
                        _rfRunTx();
                        break;
                    case 2:
                        _rfRunScan();
                        break;
                    case 3:
                        _rfRunSettings();
                        break;
                }
                dirty = true;
                _rfDrawMenu();
            }

            if (inputBackFired()) {
                inputSettle(200);
                _rfSetRx();   // restore RX before handing control back
                return;
            }
        }

        delay(5);
    }
}
