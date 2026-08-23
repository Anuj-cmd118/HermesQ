#pragma once

#include <cstring>
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_bridge.h"
#include "hizmos_ui.h"
#include "hizmos_nfc.h"         // NFC engine
#include "hizmos_ir.h"          // IR engine
#include "hizmos_rf.h"          // RF Sub-GHz engine
#include "hizmos_apps.h"        // Calculator, Pomodoro, Snake, Space Invaders
#include "hizmos_engineering.h" // 8 engineering calculators
#include "hizmos_settings.h"    // About, Display, SysInfo, Restart
#include "hizmos_notes.h"       // Notes (Browse/New/Search via Bridge)

// ─────────────────────────────────────────────────────────────
//  Generic stub — for features not yet fully ported
// ─────────────────────────────────────────────────────────────
static void stubDetail(const char* parent, const char* item) {
    inputSettle(150);
    u8g2.clearBuffer();
    uiDrawTitle("Coming Soon");
    u8g2.setFont(u8g2_font_6x10_tr);
    char line[32];
    snprintf(line, 32, "%.20s", parent);
    u8g2.drawStr(0, 26, line);
    snprintf(line, 32, "> %.18s", item);
    u8g2.drawStr(0, 38, line);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 50, "Not yet implemented");
    uiDrawHint("Any key = back");
    u8g2.sendBuffer();

    while (true) {
        inputPoll();
        uiTouchActivity();
        if (inputBackFired() || inputSelectOrLearnFired() || inputBackHeld()) break;
        delay(5);
    }
    inputSettle(100);
}

// ─────────────────────────────────────────────────────────────
//  Bridge action helpers — show result on OLED, no webpage
// ─────────────────────────────────────────────────────────────
static void showBridgeLoading(const char* title) {
    u8g2.clearBuffer();
    u8g2.setFont(HIZ_FONT_BODY);
    u8g2.drawStr(0, 10, title);
    u8g2.setFont(HIZ_FONT_TINY);
    u8g2.drawStr(0, 36, "Contacting Linux...");
    u8g2.sendBuffer();
}

static void showBridgeResult(const char* title, const String& resp) {
    String payload;
    bool ok = hizBridgeOk(resp, payload);

    u8g2.clearBuffer();
    u8g2.setFont(HIZ_FONT_BODY);
    u8g2.drawStr(0, 10, title);
    u8g2.drawHLine(0, 12, 128);
    u8g2.setFont(HIZ_FONT_TINY);
    u8g2.drawStr(0, 26, ok ? "OK" : "Error");

    char line1[22] = {0};
    char line2[22] = {0};
#if defined(ARDUINO_ARCH_ZEPHYR)
    strncpy(line1, payload.c_str(), 21);
    if (payload.length() > 21) strncpy(line2, payload.c_str() + 21, 21);
#else
    payload.substring(0, 21).toCharArray(line1, 22);
    if (payload.length() > 21) payload.substring(21, 42).toCharArray(line2, 22);
#endif
    u8g2.drawStr(0, 40, line1);
    if (line2[0]) u8g2.drawStr(0, 52, line2);
    u8g2.sendBuffer();

    while (true) {
        inputPoll();
        if (inputBackFired()) break;
        delay(5);
    }
}

static void runBridgeAction(const char* title, const char* method, const char* arg = "") {
    showBridgeLoading(title);
    String resp;
    hizBridgeCall(method, resp, arg);
    showBridgeResult(title, resp);
}

// ─────────────────────────────────────────────────────────────
//  HID actions — OLED feedback only, no browser launch
// ─────────────────────────────────────────────────────────────
static void showHidStatus()      { runBridgeAction("HID Status",   "hid_status");      }
static void startBleKeyboard()   { runBridgeAction("BLE Keyboard", "hid_start_kb");    }
static void startBleMouse()      { runBridgeAction("BLE Mouse",    "hid_start_mouse"); }

// AI Agent — run a named workflow by sending its exact trigger phrase to
// ai_agent.interpret_command() over Bridge's "agent_run" RPC. This is the
// same interpret_command() path used by voice commands, so a workflow run
// from this menu and the same phrase spoken through the S3 co-processor
// produce identical results (see main.py: rpc_agent_run / run_voice_command).
static void runAgentWorkflow(const char* title, const char* triggerPhrase) {
    runBridgeAction(title, "agent_run", triggerPhrase);
}
static void showAgentStatus()    { runBridgeAction("AI Agent", "ai_status"); }

// ─────────────────────────────────────────────────────────────
//  Generic sub-list runner
// ─────────────────────────────────────────────────────────────
static void openFeatureSubList(
    const char* title,
    const char* const items[],
    uint8_t count,
    void (*onSelect)(const char* title, const char* item)
) {
    int8_t sel   = 0;
    bool   dirty = true;
    inputSettle(150);

    while (true) {
        inputPoll();

        if (dirty) {
            dirty = false;
            drawList(items, count, sel, title);
        }

        uiTouchActivity();
        int8_t scroll = inputConsumeScrollFast();
        if (scroll && count) {
            sel = (int8_t)constrain((int)sel + scroll, 0, (int)count - 1);
            dirty = true;
        }
        if (inputSelectOrLearnFired() && count) {
            onSelect(title, items[sel]);
            dirty = true;
            inputSettle(150);
        }
        if (inputBackFired()) {
            inputSettle(150);
            break;
        }

        delay(5);
    }
}

// ─────────────────────────────────────────────────────────────
//  Per-feature select handlers
// ─────────────────────────────────────────────────────────────
static void onHidSelect(const char* title, const char* item) {
    (void)title;
    if      (strcmp(item, "start keyboard") == 0) startBleKeyboard();
    else if (strcmp(item, "start mouse")    == 0) startBleMouse();
    else if (strcmp(item, "status")         == 0) showHidStatus();
    else stubDetail("HID", item);
}

// Trigger phrases match the regex rules in ai_agent.py exactly, so picking
// a workflow here runs the identical code path a spoken command would.
static void onAiAgentSelect(const char* title, const char* item) {
    (void)title;
    if      (strcmp(item, "dev environment")     == 0) runAgentWorkflow("Dev Env",   "launch my dev environment");
    else if (strcmp(item, "presentation mode")   == 0) runAgentWorkflow("Presenting","presentation mode");
    else if (strcmp(item, "open tabs")           == 0) runAgentWorkflow("Open Tabs", "open 10 tabs");
    else if (strcmp(item, "status")              == 0) showAgentStatus();
    else stubDetail("AI Agent", item);
}

static void onGenericSelect(const char* title, const char* item) {
    stubDetail(title, item);
}
static void onNfcSelect(const char* title, const char* item) {
    (void)title;
    if      (strcmp(item, "read")  == 0) nfcAppRead();
    else if (strcmp(item, "write") == 0) nfcAppWrite();
    else if (strcmp(item, "saved") == 0) nfcAppSaved();
    else                                  nfcAppRun();
}

// IR — irAppRun() is its own full UI; sub-menu items are just quick shortcuts
static void onIrSelect(const char* title, const char* item) {
    (void)title;
    (void)item;
    // All IR navigation is inside irAppRun(); open it regardless of which
    // sub-item was tapped (remote list, learn, etc. handled internally).
    irAppRun();
}

// RF Sub-GHz — rfAppRun() handles its own menu/monitor/scan/settings
static void onRfSelect(const char* title, const char* item) {
    (void)title;
    (void)item;
    rfAppRun();
}

static void onEngineeringSelect(const char* title, const char* item) {
    (void)title;
    const char* names[] = {
        "ohm's law", "led resistor", "voltage divider",
        "battery runtime", "uart baud", "pwm frequency",
        "rf wavelength", "quarter-wave"
    };
    for (uint8_t i = 0; i < 8; i++) {
        if (strcmp(item, names[i]) == 0) { engineeringAppRun(i); return; }
    }
    engineeringAppRun(0);
}

static void onNotesSelect(const char* title, const char* item) {
    (void)title;
    if      (strcmp(item, "browse")   == 0) notesRun(0);
    else if (strcmp(item, "new note") == 0) notesRun(1);
    else if (strcmp(item, "search")   == 0) notesRun(2);
    else                                     notesRun(0);
}

static void onAppsSelect(const char* title, const char* item) {
    (void)title;
    if      (strcmp(item, "calculator")     == 0) calculatorRun();
    else if (strcmp(item, "pomodoro")       == 0) pomodoroRun();
    else if (strcmp(item, "snake")          == 0) snakeRun();
    else if (strcmp(item, "space invaders") == 0) spaceInvadersRun();
}

static void onSettingsSelect(const char* title, const char* item) {
    (void)title; (void)item;
    settingsRun();   // settings has its own internal menu
}
