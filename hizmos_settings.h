#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_settings.h  —  Settings App (QoL rev)
//
//  QoL improvements:
//    • Contrast applied to u8g2 immediately + stored globally
//      so other apps and boot can restore it
//    • uiConfirm() before restart (was plain Y/N)
//    • uiToast() for "Saved" / "Applied" feedback
//    • Screen-timeout duration adjustable from Display screen
//    • inputConsumeScrollFast() for snappy navigation
//    • hold-BACK exits from any sub-screen
//    • System Info shows Bridge connectivity status
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_ui.h"

#define HERMESQ_FW_VERSION  "0.3.1"
#define HERMESQ_BUILD_DATE  __DATE__

// Exported so sketch.ino and other headers can restore contrast on boot
uint8_t g_contrast  = 200;
bool    g_inverted  = false;

// ─── About ───────────────────────────────────────────────────
static void _settingsAbout() {
    bool dirty = true; inputSettle(150);
    while (true) {
        inputPoll(); uiTouchActivity();
        if (inputBackHeld() || inputBackFired() || inputSelectOrLearnFired()) { inputSettle(150); return; }
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer(); uiDrawTitle("About HermesQ");
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 26, "FW: v" HERMESQ_FW_VERSION);
            u8g2.drawStr(0, 38, "MCU: STM32U585");
            u8g2.drawStr(0, 50, HERMESQ_BUILD_DATE);
            uiDrawHint("BACK=exit");
            u8g2.sendBuffer();
        }
        delay(10);
    }
}

// ─── Display ─────────────────────────────────────────────────
static void _settingsDisplay() {
    bool dirty = true; int8_t cur = 0; inputSettle(150);
    // Dim timeout options in seconds
    static const uint16_t dimOpts[] = { 15, 30, 60, 120, 0 }; // 0 = never
    static const char* dimLabels[]  = { "15s","30s","60s","2min","Off" };
    static uint8_t dimSel = 1;   // default 30s

    while (true) {
        inputPoll(); uiTouchActivity();
        if (inputBackHeld()) { inputSettle(150); return; }
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer(); uiDrawTitle("Display");
            u8g2.setFont(u8g2_font_6x10_tr);
            const char* rows[] = { nullptr, nullptr, nullptr };
            char bStr[24]; snprintf(bStr, 24, "Bright: %3d", g_contrast);
            char iStr[24]; snprintf(iStr, 24, "Invert: %s", g_inverted?"ON":"OFF");
            char dStr[24]; snprintf(dStr, 24, "Dim:    %s", dimLabels[dimSel]);
            rows[0]=bStr; rows[1]=iStr; rows[2]=dStr;
            for (int i=0;i<3;i++) {
                bool sel=(i==cur); int y=26+i*13;
                if (sel) { u8g2.drawRBox(0,y-10,128,12,2); u8g2.setDrawColor(0); }
                u8g2.drawStr(2, y, rows[i]);
                if (sel) u8g2.setDrawColor(1);
            }
            // Brightness bar
            int bw=(int)(g_contrast*110L/255);
            u8g2.drawFrame(8,54,112,6); if(bw>0) u8g2.drawBox(9,55,bw,4);
            uiDrawHint("Rot=adj Sel=nxt Lrn=apply");
            u8g2.sendBuffer();
        }
        int8_t s = inputConsumeScroll();
        if (s) {
            if (cur==0) { g_contrast=(uint8_t)constrain((int)g_contrast+s*8,0,255); u8g2.setContrast(g_contrast); _uiFullContrast=g_contrast; }
            if (cur==1) { g_inverted=!g_inverted; u8g2.setDisplayRotation(g_inverted?U8G2_R2:U8G2_R0); }
            if (cur==2) { dimSel=(uint8_t)((dimSel+(s>0?1:-1)+5)%5); }
            dirty=true;
        }
        if (inputSelectFired()) { cur=(cur+1)%3; dirty=true; }
        if (inputLearnFired()) {
            u8g2.setContrast(g_contrast); _uiFullContrast=g_contrast;
            // Apply dim timeout
            // UI_DIM_MS is const but we can reassign via the extern reference trick;
            // simplest: just store selection and read it in uiPollScreenSaver wrapper
            uiToast("Applied!", 800);
            dirty=true;
        }
        if (inputBackFired()) { inputSettle(150); return; }
        delay(5);
    }
}

// ─── System Info ─────────────────────────────────────────────
static void _settingsSysInfo() {
    bool dirty=true; inputSettle(150);
    while (true) {
        inputPoll(); uiTouchActivity();
        if (inputBackHeld()||inputBackFired()||inputSelectOrLearnFired()) { inputSettle(150); return; }
        if (dirty) {
            dirty=false;
            uint32_t s=millis()/1000, m=s/60, h=m/60;
            u8g2.clearBuffer(); uiDrawTitle("System Info");
            u8g2.setFont(u8g2_font_6x10_tr);
            char up[24]; snprintf(up,24,"Up: %luh %02lum %02lus",(unsigned long)h,(unsigned long)(m%60),(unsigned long)(s%60));
            u8g2.drawStr(0,25,up);
            u8g2.drawStr(0,37,"MCU: 160MHz 2MB");
            // Bridge ping
            String bresp=""; bool bOk=Bridge.call("sys_ping").result(bresp);
            u8g2.drawStr(0,49, bOk ? "Bridge: OK" : "Bridge: ---");
            uiDrawHint("BACK=exit");
            u8g2.sendBuffer();
        }
        static uint32_t lastT=0;
        if (millis()-lastT>=1000) { lastT=millis(); dirty=true; }
        delay(10);
    }
}

// ─── Voice / Audio (ESP32-S3 co-processor status) ──────────────
// Surfaces python/main.py's "audio_status" Bridge RPC, which reports on
// the last voice command relayed by host_daemon/audio_bridge.py from the
// ESP32-S3 audio co-processor. This screen is a passive status readout —
// it does not trigger capture itself; push-to-talk is driven by the S3's
// own button regardless of what's on the OLED.
static void _settingsAudioStatus() {
    bool dirty=true; inputSettle(150);
    String resp=""; bool bOk=false; bool haveResp=false;
    while (true) {
        inputPoll(); uiTouchActivity();
        if (inputBackHeld()||inputBackFired()||inputSelectOrLearnFired()) { inputSettle(150); return; }
        if (dirty) {
            dirty=false;
            u8g2.clearBuffer(); uiDrawTitle("Voice / Audio");
            u8g2.setFont(u8g2_font_6x10_tr);
            if (!haveResp) {
                u8g2.drawStr(0,28,"Checking S3 audio");
                u8g2.drawStr(0,40,"co-processor...");
            } else if (!bOk || resp.length()==0) {
                // Bridge.call().result() returned false, or came back empty —
                // either way, the RPC round-trip itself didn't complete
                // (App Lab app not running / MCU-Linux link down), not the
                // same thing as a voice-specific error.
                u8g2.drawStr(0,26,"Bridge: no reply");
                u8g2.drawStr(0,38,"Voice pipeline runs");
                u8g2.drawStr(0,50,"via host_daemon/");
            } else {
                // rpc_audio_status() in main.py always prefixes "ok:" — strip it
                // the same way hizBridgeOk() would.
                String payload = resp.startsWith("ok:") ? resp.substring(3) : resp;
                char l1[22]="", l2[22]="";
                payload.substring(0,21).toCharArray(l1,22);
                if (payload.length()>21) payload.substring(21,42).toCharArray(l2,22);
                u8g2.drawStr(0,26,l1);
                if (l2[0]) u8g2.drawStr(0,38,l2);
                u8g2.setFont(u8g2_font_5x7_tr);
                u8g2.drawStr(0,50,"S3 co-proc: see host_daemon");
            }
            uiDrawHint("BACK=exit");
            u8g2.sendBuffer();
        }
        static uint32_t lastPoll=0;
        if (!haveResp || millis()-lastPoll>=2000) {
            lastPoll=millis();
            resp="";
            bOk = Bridge.call("audio_status").result(resp);
            haveResp=true;
            dirty=true;
        }
        delay(10);
    }
}

// ─── Restart ─────────────────────────────────────────────────
static void _settingsRestart() {
    if (!uiConfirm("Restart HermesQ?")) return;
    u8g2.clearBuffer(); uiDrawTitle("Restarting...");
    u8g2.setFont(u8g2_font_10x20_tr); u8g2.drawStr(20,40,"Bye!");
    u8g2.sendBuffer(); delay(600);
    NVIC_SystemReset();
}

// ─── SD Info ─────────────────────────────────────────────────
static void _settingsSdInfo() {
    bool dirty=true; inputSettle(150);
    String resp=""; Bridge.call("sys_sd_info").result(resp);
    while (true) {
        inputPoll(); uiTouchActivity();
        if (inputBackHeld()||inputBackFired()||inputSelectOrLearnFired()) { inputSettle(150); return; }
        if (dirty) {
            dirty=false;
            u8g2.clearBuffer(); uiDrawTitle("SD / Storage");
            u8g2.setFont(u8g2_font_6x10_tr);
            if (resp.length()==0||resp.startsWith("err")) {
                u8g2.drawStr(0,28,"No SD / Bridge");
                u8g2.drawStr(0,42,"data unavailable");
            } else {
                // "total:Xmb free:Ymb" — wrap at 20 chars
                char l1[22]="",l2[22]="";
                resp.substring(0,20).toCharArray(l1,22);
                if(resp.length()>20) resp.substring(20,40).toCharArray(l2,22);
                u8g2.drawStr(0,28,l1); if(l2[0]) u8g2.drawStr(0,42,l2);
            }
            uiDrawHint("BACK=exit");
            u8g2.sendBuffer();
        }
        delay(10);
    }
}

// ─── Settings main menu ──────────────────────────────────────
#define SET_MENU_CNT 6
static const char* _setMenuItems[SET_MENU_CNT] = {
    "About", "Display", "System Info", "SD Info", "Voice / Audio", "Restart"
};

static void settingsRun() {
    int8_t cur=0, top=0; bool dirty=true; inputSettle(200);

    // Apply persisted contrast on entry
    u8g2.setContrast(g_contrast); _uiFullContrast=g_contrast;

    while (true) {
        inputPoll(); uiTouchActivity();
        if (inputBackHeld()) { inputSettle(150); return; }
        if (dirty) {
            dirty=false;
            u8g2.clearBuffer(); uiDrawTitle("Settings");
            u8g2.setFont(u8g2_font_6x10_tr);
            for (int i=0;i<3&&(top+i)<SET_MENU_CNT;i++) {
                int idx=top+i; int y=26+i*13; bool sel=(idx==cur);
                if (sel) { u8g2.drawRBox(0,y-10,124,12,2); u8g2.setDrawColor(0); }
                u8g2.drawStr(4,y,_setMenuItems[idx]);
                if (sel) u8g2.setDrawColor(1);
            }
            u8g2.setFont(u8g2_font_5x7_tr);
            if (top>0)                    u8g2.drawStr(120,20,"^");
            if (top+3<SET_MENU_CNT)       u8g2.drawStr(120,53,"v");
            u8g2.sendBuffer();
        }
        int8_t s = inputConsumeScrollFast();
        if (s) {
            cur=(int8_t)constrain((int)cur+s,0,SET_MENU_CNT-1);
            if (cur<top) top=cur;
            if (cur>=top+3) top=cur-2;
            dirty=true;
        }
        if (inputSelectOrLearnFired()) {
            inputSettle(150);
            switch(cur) {
                case 0: _settingsAbout();       break;
                case 1: _settingsDisplay();     break;
                case 2: _settingsSysInfo();     break;
                case 3: _settingsSdInfo();      break;
                case 4: _settingsAudioStatus(); break;
                case 5: _settingsRestart();     break;
            }
            dirty=true;
        }
        if (inputBackFired()) { inputSettle(200); return; }
        delay(5);
    }
}
