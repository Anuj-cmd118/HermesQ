#pragma once
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_feature.h"

// ─────────────────────────────────────────────────────────────
//  Sub-menu item lists
// ─────────────────────────────────────────────────────────────
static const char* infraredItems[] = {
    "remotes", "learn", "emit last"
};
static const char* subghzItems[] = {
    "monitor", "transmit", "freq scan", "settings"
};
static const char* engineeringItems[] = {
    "ohm's law", "led resistor", "voltage divider",
    "battery runtime", "uart baud", "pwm frequency",
    "rf wavelength", "quarter-wave"
};
static const char* nrfItems[] = {
    "scanner", "jammer", "spec channel"
};
static const char* wifiItems[] = {
    "scan", "deauth", "packet analyzer"
};
static const char* hidItems[] = {
    "start keyboard", "start mouse", "status"
};
// AI Agent — real Bridge-backed workflows (see ai_agent.py / workflows.json).
// Picking one sends its exact trigger phrase to interpret_command() over
// Bridge's "agent_run" RPC — the identical path used by voice commands
// (host_daemon/audio_bridge.py -> POST /api/agent/voice -> run_voice_command()).
static const char* aiAgentItems[] = {
    "dev environment", "presentation mode", "open tabs", "status"
};
static const char* notesItems[] = {
    "browse", "new note", "search"
};
static const char* appsItems[] = {
    "calculator", "pomodoro", "snake", "space invaders"
};
static const char* settingsItems[] = {
    "about", "restart", "sd format", "sd info"
};
// NFC sub-menu — shortcuts into the real NFC engine
static const char* nfcItems[] = {
    "read", "write", "saved"
};
static const char* badusbItems[] = {
    "run script", "keyboard", "download"
};

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
static void openSubList(const char* title, const char* const items[], uint8_t count) {
    openFeatureSubList(title, items, count, onGenericSelect);
}

// ─────────────────────────────────────────────────────────────
//  handleMenuAction — called from hizmos_mainmenu.h with the
//  selected main-menu index (0-based, matches icon order).
// ─────────────────────────────────────────────────────────────
void handleMenuAction(int index) {
    switch (index) {

        // 0 — Infrared: open IR engine directly
        case 0:
            // Sub-menu gives the user a choice, but all paths call irAppRun()
            openFeatureSubList("Infrared",
                infraredItems, sizeof(infraredItems) / sizeof(infraredItems[0]),
                onIrSelect);
            break;

        // 1 — Sub-GHz: open RF engine
        case 1:
            openFeatureSubList("Sub-GHz", subghzItems,
                sizeof(subghzItems) / sizeof(subghzItems[0]),
                onRfSelect);
            break;

        // 2 — Engineering: real calculators
        case 2:
            openFeatureSubList("Engineering", engineeringItems,
                sizeof(engineeringItems) / sizeof(engineeringItems[0]),
                onEngineeringSelect);
            break;

        // 3 — nRF Tools (stub)
        case 3:
            openSubList("nRF Tools", nrfItems,
                sizeof(nrfItems) / sizeof(nrfItems[0]));
            break;

        // 4 — WiFi (stub)
        case 4:
            openSubList("WiFi", wifiItems,
                sizeof(wifiItems) / sizeof(wifiItems[0]));
            break;

        // 5 — HID: real BLE HID actions over Bridge (hid_start_kb/hid_start_mouse/hid_status)
        case 5:
            openFeatureSubList("HID", hidItems,
                sizeof(hidItems) / sizeof(hidItems[0]),
                onHidSelect);
            break;

        // 6 — AI Agent: real workflows sent over Bridge's agent_run RPC to
        // ai_agent.interpret_command() — the same path used by voice commands.
        case 6:
            openFeatureSubList("AI Agent", aiAgentItems,
                sizeof(aiAgentItems) / sizeof(aiAgentItems[0]),
                onAiAgentSelect);
            break;

        // 7 — Notes: real browse/create/search
        case 7:
            openFeatureSubList("Notes", notesItems,
                sizeof(notesItems) / sizeof(notesItems[0]),
                onNotesSelect);
            break;

        // 8 — Apps: Calculator, Pomodoro, Snake, Space Invaders
        case 8:
            openFeatureSubList("Apps", appsItems,
                sizeof(appsItems) / sizeof(appsItems[0]),
                onAppsSelect);
            break;

        // 9 — Settings: About, Display, SysInfo, SD, Restart
        case 9:
            settingsRun();
            break;

        // 10 — NFC: routes to real NFC engine via onNfcSelect
        case 10:
            openFeatureSubList("NFC", nfcItems,
                sizeof(nfcItems) / sizeof(nfcItems[0]),
                onNfcSelect);
            break;

        // 11 — Bad USB (stub)
        case 11:
            openSubList("Bad USB", badusbItems,
                sizeof(badusbItems) / sizeof(badusbItems[0]));
            break;
    }
}
