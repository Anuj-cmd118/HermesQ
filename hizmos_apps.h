#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_apps.h  —  Built-in Apps for HermesQ  (QoL rev)
//
//  Apps:
//    calculatorRun()     — 4-op calculator, hold-SELECT to advance
//    pomodoroRun()       — Pomodoro timer, accurate tick
//    snakeRun()          — Snake, accelerated scroll, high score
//    spaceInvadersRun()  — Space Invaders, waves, high score
//
//  QoL improvements vs previous rev:
//    • All scroll calls use inputConsumeScrollFast() for menus,
//      inputConsumeScroll() where precision matters (calc digit)
//    • Hold-BACK (≥800ms) returns to main menu from any screen
//    • High scores persisted in RAM via uiHighScoreGet/Set
//    • uiToast() used for feedback instead of blocking screens
//    • Pomodoro uses absolute end-time so timer is drift-free
//    • Snake: direction reversal into self blocked correctly
//    • Calc: hold SELECT appends digit repeatedly (fast entry)
//    • Game-over screen shows current AND best score
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_ui.h"

// ─── Shared draw helpers (re-export uiDraw* as local aliases) ─
static void _appTitle(const char* t) { uiDrawTitle(t); }
static void _appHint(const char* h)  { uiDrawHint(h);  }


// ╔═══════════════════════════════════════════════════════════╗
// ║  CALCULATOR                                               ║
// ║  Encoder scrolls digit 0-9.                               ║
// ║  SELECT (tap)  = append current digit                     ║
// ║  SELECT (hold) = advance state (operand→op→operand→=)     ║
// ║  LEARN         = decimal point / move to op if no dot yet ║
// ║  BACK (tap)    = backspace                                ║
// ║  BACK (hold)   = exit                                     ║
// ╚═══════════════════════════════════════════════════════════╝
#define CALC_S_INPUT_A  0
#define CALC_S_OP       1
#define CALC_S_INPUT_B  2
#define CALC_S_RESULT   3
#define CALC_S_ERROR    4

static const char _calcOps[] = { '+', '-', '*', '/' };
#define CALC_OP_CNT 4

static char   _calcBufA[20] = "";
static char   _calcBufB[20] = "";
static char   _calcResult[24] = "";
static int8_t _calcOpIdx  = 0;
static int8_t _calcDigit  = 0;
static bool   _calcDotA   = false;
static bool   _calcDotB   = false;

static void _calcAppend(char* buf, char c) {
    size_t n = strlen(buf);
    if (n < 18) { buf[n] = c; buf[n + 1] = '\0'; }
}
static void _calcBackspace(char* buf, bool& dot) {
    size_t n = strlen(buf);
    if (!n) return;
    if (buf[n - 1] == '.') dot = false;
    buf[n - 1] = '\0';
}
static void _calcCompute() {
    double a = atof(_calcBufA), b = atof(_calcBufB), r = 0;
    char op = _calcOps[_calcOpIdx];
    if (op == '/' && b == 0.0) { strcpy(_calcResult, "Div/0!"); return; }
    switch (op) {
        case '+': r = a + b; break; case '-': r = a - b; break;
        case '*': r = a * b; break; case '/': r = a / b; break;
    }
    if (r == (long)r && r >= -999999 && r <= 9999999)
        snprintf(_calcResult, 24, "%.0f", r);
    else
        snprintf(_calcResult, 24, "%.4g", r);
}

static void _calcDraw(int state) {
    u8g2.clearBuffer();
    _appTitle("Calculator");
    u8g2.setFont(u8g2_font_6x10_tr);
    if (state == CALC_S_INPUT_A) {
        char d[24]; snprintf(d, 24, "A: %s[%d]", _calcBufA, _calcDigit);
        u8g2.drawStr(0, 26, d);
        _appHint("Rot=dig Sel=add Hlд=next");
    } else if (state == CALC_S_OP) {
        u8g2.drawStr(0, 26, _calcBufA);
        char op[4] = { ' ', _calcOps[_calcOpIdx], ' ', 0 };
        u8g2.drawStr(0, 40, op);
        _appHint("Rot=op  Hld=confirm");
    } else if (state == CALC_S_INPUT_B) {
        char l[32]; snprintf(l, 32, "%s %c", _calcBufA, _calcOps[_calcOpIdx]);
        u8g2.drawStr(0, 22, l);
        char d[24]; snprintf(d, 24, "B: %s[%d]", _calcBufB, _calcDigit);
        u8g2.drawStr(0, 36, d);
        _appHint("Rot=dig Sel=add Hld==");
    } else if (state == CALC_S_RESULT) {
        char expr[32];
        snprintf(expr, 32, "%s %c %s", _calcBufA, _calcOps[_calcOpIdx], _calcBufB);
        u8g2.drawStr(0, 22, expr);
        u8g2.drawStr(0, 36, "=");
        u8g2.setFont(u8g2_font_10x20_tr);
        u8g2.drawStr(0, 52, _calcResult);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawHLine(0, 55, 128);
        u8g2.drawStr(0, 63, "Hld=reuse  BACK=new");
        u8g2.sendBuffer(); return;
    } else if (state == CALC_S_ERROR) {
        u8g2.drawStr(0, 30, _calcResult);
        _appHint("BACK=clear");
    }
    u8g2.sendBuffer();
}

static void calculatorRun() {
    memset(_calcBufA, 0, sizeof(_calcBufA));
    memset(_calcBufB, 0, sizeof(_calcBufB));
    memset(_calcResult, 0, sizeof(_calcResult));
    _calcOpIdx = 0; _calcDigit = 0;
    _calcDotA = false; _calcDotB = false;
    int state = CALC_S_INPUT_A;
    bool dirty = true;
    inputSettle(200);

    while (true) {
        inputPoll();
        uiTouchActivity();
        if (dirty) { dirty = false; _calcDraw(state); }

        // Hold-BACK exits anywhere
        if (inputBackHeld()) { inputSettle(150); return; }

        int8_t s = inputConsumeScroll();

        switch (state) {
            case CALC_S_INPUT_A:
                if (s) { _calcDigit = (int8_t)(((_calcDigit + s) % 10 + 10) % 10); dirty = true; }
                if (inputSelectFired()) {
                    _calcAppend(_calcBufA, '0' + _calcDigit);
                    _calcDigit = 0; dirty = true;
                }
                // Hold SELECT → advance to operator selection
                if (inputSelectHeld() && strlen(_calcBufA)) {
                    state = CALC_S_OP; _calcDigit = 0; dirty = true;
                }
                if (inputLearnFired()) {
                    if (!_calcDotA && strlen(_calcBufA)) { _calcAppend(_calcBufA, '.'); _calcDotA = true; dirty = true; }
                }
                if (inputBackFired()) {
                    if (strlen(_calcBufA)) { _calcBackspace(_calcBufA, _calcDotA); dirty = true; }
                    else { inputSettle(150); return; }
                }
                break;

            case CALC_S_OP:
                if (s) { _calcOpIdx = (int8_t)(((_calcOpIdx + s) % CALC_OP_CNT + CALC_OP_CNT) % CALC_OP_CNT); dirty = true; }
                if (inputSelectFired() || inputLearnFired()) { state = CALC_S_INPUT_B; _calcDigit = 0; dirty = true; }
                if (inputSelectHeld()) { state = CALC_S_INPUT_B; _calcDigit = 0; dirty = true; }
                if (inputBackFired()) { state = CALC_S_INPUT_A; dirty = true; }
                break;

            case CALC_S_INPUT_B:
                if (s) { _calcDigit = (int8_t)(((_calcDigit + s) % 10 + 10) % 10); dirty = true; }
                if (inputSelectFired()) {
                    _calcAppend(_calcBufB, '0' + _calcDigit);
                    _calcDigit = 0; dirty = true;
                }
                // Hold SELECT → compute result
                if (inputSelectHeld() && strlen(_calcBufB)) {
                    _calcCompute(); state = CALC_S_RESULT; dirty = true;
                }
                if (inputLearnFired()) {
                    if (!_calcDotB && strlen(_calcBufB)) { _calcAppend(_calcBufB, '.'); _calcDotB = true; dirty = true; }
                    else if (strlen(_calcBufB)) { _calcCompute(); state = CALC_S_RESULT; dirty = true; }
                }
                if (inputBackFired()) {
                    if (strlen(_calcBufB)) { _calcBackspace(_calcBufB, _calcDotB); dirty = true; }
                    else { state = CALC_S_OP; dirty = true; }
                }
                break;

            case CALC_S_RESULT:
                // Hold SELECT → reuse result as A
                if (inputSelectHeld()) {
                    strncpy(_calcBufA, _calcResult, 18);
                    memset(_calcBufB, 0, sizeof(_calcBufB));
                    _calcDotA = (strchr(_calcBufA, '.') != nullptr);
                    _calcDotB = false;
                    state = CALC_S_OP; dirty = true;
                }
                if (inputBackFired()) {
                    memset(_calcBufA, 0, sizeof(_calcBufA));
                    memset(_calcBufB, 0, sizeof(_calcBufB));
                    _calcDotA = false; _calcDotB = false;
                    state = CALC_S_INPUT_A; dirty = true;
                }
                break;

            case CALC_S_ERROR:
                if (inputBackFired()) {
                    memset(_calcBufA, 0, sizeof(_calcBufA));
                    memset(_calcBufB, 0, sizeof(_calcBufB));
                    state = CALC_S_INPUT_A; dirty = true;
                }
                break;
        }
        delay(5);
    }
}


// ╔═══════════════════════════════════════════════════════════╗
// ║  POMODORO TIMER                                           ║
// ║  Drift-free: stores absolute end time, not elapsed.       ║
// ║  SELECT = pause/resume  LEARN = skip  BACK(hold) = exit   ║
// ╚═══════════════════════════════════════════════════════════╝
#define POMO_S_SETUP  0
#define POMO_S_WORK   1
#define POMO_S_SHORT  2
#define POMO_S_LONG   3
#define POMO_S_PAUSED 4
#define POMO_S_DONE   5

static uint8_t _pomoWork    = 25;
static uint8_t _pomoShort   = 5;
static uint8_t _pomoLong    = 15;
static uint8_t _pomoSession = 0;
static uint8_t _pomoSetupCur = 0;

static uint32_t _pomoEndMs       = 0;
static uint32_t _pomoRemainingMs = 0;  // remaining at pause time
static int      _pomoPrevState   = POMO_S_WORK;
static bool     _pomoPaused      = false;

static void _pomoDraw(int state) {
    u8g2.clearBuffer();
    if (state == POMO_S_SETUP) {
        _appTitle("Pomodoro Setup");
        u8g2.setFont(u8g2_font_6x10_tr);
        const char* labels[] = { "Work min:", "Short brk:", "Long brk:" };
        uint8_t vals[] = { _pomoWork, _pomoShort, _pomoLong };
        for (int i = 0; i < 3; i++) {
            int y = 26 + i * 13; bool sel = (i == _pomoSetupCur);
            if (sel) { u8g2.drawRBox(0, y-10, 128, 12, 2); u8g2.setDrawColor(0); }
            char l[24]; snprintf(l, 24, "%-10s %2d", labels[i], vals[i]);
            u8g2.drawStr(2, y, l);
            if (sel) u8g2.setDrawColor(1);
        }
        _appHint("Rot=val Sel=next BK(hld)=exit");
    } else {
        const char* phase =
            (state==POMO_S_WORK || (_pomoPaused && _pomoPrevState==POMO_S_WORK))   ? "WORK"      :
            (state==POMO_S_SHORT || (_pomoPaused && _pomoPrevState==POMO_S_SHORT)) ? "SHORT BRK" :
            (state==POMO_S_LONG  || (_pomoPaused && _pomoPrevState==POMO_S_LONG))  ? "LONG BRK"  :
                                                                                      "DONE!";
        _appTitle("Pomodoro");
        u8g2.setFont(u8g2_font_6x10_tr);
        char sl[24]; snprintf(sl, 24, "Sessions: %d", _pomoSession);
        u8g2.drawStr(0, 22, sl);
        u8g2.setFont(u8g2_font_10x20_tr);
        u8g2.drawStr(0, 42, phase);

        if (state != POMO_S_DONE) {
            uint32_t remMs = _pomoPaused ? _pomoRemainingMs
                                         : (_pomoEndMs > millis() ? _pomoEndMs - millis() : 0);
            uint32_t remS  = remMs / 1000;
            char ts[12]; snprintf(ts, 12, "%02lu:%02lu", (unsigned long)(remS/60), (unsigned long)(remS%60));
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(80, 42, ts);
            // Progress bar
            uint8_t dur = (state==POMO_S_WORK||(_pomoPaused&&_pomoPrevState==POMO_S_WORK)) ? _pomoWork
                        : (state==POMO_S_SHORT||(_pomoPaused&&_pomoPrevState==POMO_S_SHORT)) ? _pomoShort : _pomoLong;
            uint32_t totalMs = (uint32_t)dur * 60000UL;
            uint32_t elapsedMs = (totalMs > remMs) ? totalMs - remMs : 0;
            int bw = (int)((elapsedMs * 110UL) / totalMs);
            u8g2.drawFrame(8,47,112,6);
            if (bw > 0) u8g2.drawBox(9, 48, bw, 4);
        }
        if (_pomoPaused)             _appHint("Sel=resume Lrn=skip BK=stop");
        else if (state==POMO_S_DONE) _appHint("Sel=restart  BACK=exit");
        else                          _appHint("Sel=pause Lrn=skip BK=stop");
    }
    u8g2.sendBuffer();
}

static void pomodoroRun() {
    int state = POMO_S_SETUP;
    _pomoSetupCur = 0; _pomoSession = 0; _pomoPaused = false;
    bool dirty = true;
    inputSettle(200);

    auto startPhase = [&](int ph) {
        uint8_t dur = (ph==POMO_S_WORK) ? _pomoWork : (ph==POMO_S_SHORT) ? _pomoShort : _pomoLong;
        _pomoEndMs = millis() + (uint32_t)dur * 60000UL;
        _pomoPaused = false; state = ph; dirty = true;
    };

    uint32_t lastRedrawMs = 0;

    while (true) {
        inputPoll();
        uiTouchActivity();

        // Hold-BACK exits from anywhere
        if (inputBackHeld()) { inputSettle(150); return; }

        if (state == POMO_S_SETUP) {
            if (dirty) { dirty = false; _pomoDraw(state); }
            int8_t s = inputConsumeScroll();
            if (s) {
                if      (_pomoSetupCur==0) _pomoWork  = (uint8_t)constrain((int)_pomoWork  + s, 1, 90);
                else if (_pomoSetupCur==1) _pomoShort = (uint8_t)constrain((int)_pomoShort + s, 1, 30);
                else                        _pomoLong  = (uint8_t)constrain((int)_pomoLong  + s, 1, 60);
                dirty = true;
            }
            if (inputSelectOrLearnFired()) {
                _pomoSetupCur++;
                if (_pomoSetupCur >= 3) startPhase(POMO_S_WORK);
                dirty = true;
            }
        } else {
            // Accurate tick: check absolute end time
            if (!_pomoPaused && state != POMO_S_DONE && millis() >= _pomoEndMs) {
                if (state == POMO_S_WORK) {
                    _pomoSession++;
                    uiToast(_pomoSession % 4 == 0 ? "Long break!" : "Short break!", 1500);
                    startPhase(_pomoSession % 4 == 0 ? POMO_S_LONG : POMO_S_SHORT);
                } else {
                    uiToast("Back to work!", 1500);
                    startPhase(POMO_S_WORK);
                }
            }

            // Redraw every second only
            if (!dirty && !_pomoPaused && millis() - lastRedrawMs >= 1000) {
                lastRedrawMs = millis(); dirty = true;
            }
            if (dirty) { dirty = false; _pomoDraw(state); }

            if (inputSelectFired()) {
                if (state == POMO_S_DONE) { startPhase(POMO_S_WORK); _pomoSession = 0; }
                else if (_pomoPaused) {
                    // Resume: restore absolute end time
                    _pomoEndMs = millis() + _pomoRemainingMs;
                    _pomoPaused = false;
                } else {
                    _pomoRemainingMs = (_pomoEndMs > millis()) ? _pomoEndMs - millis() : 0;
                    _pomoPrevState = state; _pomoPaused = true; state = POMO_S_PAUSED;
                }
                dirty = true;
            }
            if (inputLearnFired()) {
                bool wasWork = (state==POMO_S_WORK || (_pomoPaused && _pomoPrevState==POMO_S_WORK));
                if (wasWork) { _pomoSession++; startPhase(_pomoSession%4==0 ? POMO_S_LONG : POMO_S_SHORT); }
                else          startPhase(POMO_S_WORK);
                dirty = true;
            }
            if (inputBackFired()) { inputSettle(150); return; }
        }
        delay(20);
    }
}


// ╔═══════════════════════════════════════════════════════════╗
// ║  SNAKE                                                    ║
// ║  Accelerated scroll for steering.                         ║
// ║  Direction reversal into self is blocked (not just same). ║
// ║  High score persisted in RAM across sessions.             ║
// ╚═══════════════════════════════════════════════════════════╝
#define SNAKE_W   21
#define SNAKE_H   9
#define SNAKE_MAX 100
#define CELL_PX   5
#define FIELD_X   3
#define FIELD_Y   14

struct SnakeCell { int8_t x, y; };

static SnakeCell _snBody[SNAKE_MAX];
static uint8_t   _snLen    = 3;
static int8_t    _snDx     = 1, _snDy = 0;
static SnakeCell _snFood   = { 10, 4 };
static uint16_t  _snScore  = 0;
static bool      _snAlive  = true;
static bool      _snPaused = false;
static uint8_t   _snSpeed  = 1;
static const uint16_t _snIntervalMs[] = { 300, 180, 90 };
static const char*    _snSpeedNames[] = { "Slow", "Med", "Fast" };

// Pending direction — buffered one step ahead to prevent input lag
static int8_t _snNextDx = 1, _snNextDy = 0;

static void _snPlaceFood() {
    for (uint8_t t = 0; t < 50; t++) {
        int8_t fx = (int8_t)(random(0, SNAKE_W));
        int8_t fy = (int8_t)(random(0, SNAKE_H));
        bool hit = false;
        for (uint8_t i = 0; i < _snLen; i++)
            if (_snBody[i].x == fx && _snBody[i].y == fy) { hit = true; break; }
        if (!hit) { _snFood = { fx, fy }; return; }
    }
}

static void _snReset() {
    _snLen = 3;
    for (uint8_t i = 0; i < _snLen; i++) _snBody[i] = { (int8_t)(5 - (int8_t)i), 4 };
    _snDx = 1; _snDy = 0; _snNextDx = 1; _snNextDy = 0;
    _snScore = 0; _snAlive = true; _snPaused = false;
    _snPlaceFood();
}

static void _snDraw() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    uint16_t best = uiHighScoreGet("snake");
    char hdr[32]; snprintf(hdr, 32, "Snake %s  %d|B:%d", _snSpeedNames[_snSpeed], _snScore, best);
    u8g2.drawStr(0, 8, hdr);
    u8g2.drawHLine(0, 10, 128);
    u8g2.drawFrame(FIELD_X-1, FIELD_Y-1, SNAKE_W*CELL_PX+2, SNAKE_H*CELL_PX+2);
    // Food — blinking every 500ms
    if ((millis() / 500) & 1) {
        u8g2.drawBox(FIELD_X + _snFood.x*CELL_PX+1, FIELD_Y + _snFood.y*CELL_PX+1, 3, 3);
    } else {
        u8g2.drawFrame(FIELD_X + _snFood.x*CELL_PX, FIELD_Y + _snFood.y*CELL_PX, CELL_PX, CELL_PX);
    }
    for (uint8_t i = 0; i < _snLen; i++) {
        int px = FIELD_X + _snBody[i].x*CELL_PX;
        int py = FIELD_Y + _snBody[i].y*CELL_PX;
        if (i == 0) u8g2.drawBox(px, py, CELL_PX, CELL_PX);
        else        u8g2.drawFrame(px+1, py+1, CELL_PX-2, CELL_PX-2);
    }
    if (_snPaused) {
        u8g2.drawBox(35,25,58,14); u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_6x10_tr); u8g2.drawStr(40,35,"PAUSED"); u8g2.setDrawColor(1);
    }
    u8g2.sendBuffer();
}

static void _snDrawGameOver() {
    uint16_t best = uiHighScoreGet("snake");
    u8g2.clearBuffer();
    _appTitle("Snake");
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.drawStr(14, 36, "GAME OVER");
    u8g2.setFont(u8g2_font_6x10_tr);
    char sc[28]; snprintf(sc, 28, "Score:%d  Best:%d", _snScore, best);
    u8g2.drawStr(4, 50, sc);
    _appHint("Sel=replay  BACK=exit");
    u8g2.sendBuffer();
}

static void snakeRun() {
    _snReset();
    randomSeed(analogRead(A0) ^ millis());
    uint32_t lastMoveMs = millis();
    bool dirty = true;
    inputSettle(200);

    while (true) {
        inputPoll();
        uiTouchActivity();
        if (inputBackHeld()) { inputSettle(150); return; }

        if (!_snAlive) {
            if (dirty) { dirty = false; _snDrawGameOver(); }
            if (inputSelectOrLearnFired()) { _snReset(); dirty = true; }
            if (inputBackFired()) { inputSettle(150); return; }
            delay(10); continue;
        }

        // Steering — buffer next direction, block 180° reversal
        int8_t s = inputConsumeScrollFast();
        if (s > 0) {
            // CW turn relative to heading
            int8_t nx = _snDy, ny = -_snDx;
            if (!(nx == -_snDx && ny == -_snDy)) { _snNextDx = nx; _snNextDy = ny; }
        } else if (s < 0) {
            // CCW turn
            int8_t nx = -_snDy, ny = _snDx;
            if (!(nx == -_snDx && ny == -_snDy)) { _snNextDx = nx; _snNextDy = ny; }
        }

        if (inputSelectFired()) { _snPaused = !_snPaused; dirty = true; }
        if (inputLearnFired())  { _snSpeed  = (_snSpeed + 1) % 3; }
        if (inputBackFired())   { inputSettle(150); return; }

        if (!_snPaused && millis() - lastMoveMs >= _snIntervalMs[_snSpeed]) {
            lastMoveMs = millis();
            // Apply buffered direction
            _snDx = _snNextDx; _snDy = _snNextDy;
            int8_t nx = (int8_t)((_snBody[0].x + _snDx + SNAKE_W) % SNAKE_W);
            int8_t ny = (int8_t)((_snBody[0].y + _snDy + SNAKE_H) % SNAKE_H);
            // Self collision (skip head itself = index 0)
            bool selfHit = false;
            for (uint8_t i = 1; i < _snLen; i++)
                if (_snBody[i].x == nx && _snBody[i].y == ny) { selfHit = true; break; }
            if (selfHit) {
                uiHighScoreSet("snake", _snScore);
                _snAlive = false; dirty = true; delay(10); continue;
            }
            bool ate = (nx == _snFood.x && ny == _snFood.y);
            if (ate && _snLen < SNAKE_MAX) _snLen++;
            for (uint8_t i = _snLen-1; i > 0; i--) _snBody[i] = _snBody[i-1];
            _snBody[0] = { nx, ny };
            if (ate) { _snScore++; _snPlaceFood(); }
            dirty = true;
        }
        if (dirty) { dirty = false; _snDraw(); }
        delay(5);
    }
}


// ╔═══════════════════════════════════════════════════════════╗
// ║  SPACE INVADERS                                           ║
// ║  Accelerated scroll for player movement.                  ║
// ║  High score tracked. Wave speed increases correctly.      ║
// ╚═══════════════════════════════════════════════════════════╝
#define SI_W        128
#define SI_PY       50
#define SI_INV_COLS  8
#define SI_INV_ROWS  3
#define SI_INV_CNT  (SI_INV_COLS*SI_INV_ROWS)
#define SI_BULLET_CNT 3
#define SI_GRID_X0   6
#define SI_GRID_Y0  14
#define SI_GCELL_W  14
#define SI_GCELL_H   8

struct SiBullet { int8_t x; int8_t y; bool active; bool player; };

static int8_t   _siPx          = 60;
static uint8_t  _siPSpeed      = 2;
static bool     _siInvAlive[SI_INV_CNT];
static int      _siInvOffX     = 0;
static int8_t   _siInvDir      = 1;
static uint8_t  _siInvAliveN   = SI_INV_CNT;
static SiBullet _siBullets[SI_BULLET_CNT];
static uint16_t _siScore       = 0;
static uint8_t  _siLives       = 3;
static uint8_t  _siWave        = 1;
static bool     _siAlive       = true;
static bool     _siPaused      = false;
static uint32_t _siLastMoveMs  = 0;
static uint32_t _siInvMoveMs   = 0;
static uint32_t _siInvFireMs   = 0;
static uint8_t  _siShootCool   = 0;
static int8_t   _siInvDropY    = 0;   // cumulative invader drop in pixels

static void _siReset(bool full) {
    if (full) { _siScore = 0; _siLives = 3; _siWave = 1; }
    _siPx = 60; _siInvOffX = 0; _siInvDir = 1; _siInvDropY = 0;
    _siInvAliveN = SI_INV_CNT;
    for (uint8_t i = 0; i < SI_INV_CNT; i++) _siInvAlive[i] = true;
    for (uint8_t i = 0; i < SI_BULLET_CNT; i++) _siBullets[i].active = false;
    _siAlive = true; _siPaused = false; _siShootCool = 0;
}

static void _siDrawInvader(int x, int y, bool alt) {
    if (!alt) {
        u8g2.drawPixel(x+1,y); u8g2.drawPixel(x+2,y); u8g2.drawPixel(x+3,y);
        u8g2.drawBox(x,y+1,5,2);
        u8g2.drawPixel(x,y+3); u8g2.drawPixel(x+2,y+3); u8g2.drawPixel(x+4,y+3);
    } else {
        u8g2.drawPixel(x,y); u8g2.drawPixel(x+2,y); u8g2.drawPixel(x+4,y);
        u8g2.drawBox(x,y+1,5,2);
        u8g2.drawPixel(x+1,y+3); u8g2.drawPixel(x+3,y+3);
    }
}

static void _siDraw() {
    bool alt = ((millis()/400)&1);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    uint16_t best = uiHighScoreGet("si");
    char hdr[36]; snprintf(hdr, 36, "Sc:%d B:%d Lv:%d W:%d", _siScore, best, _siLives, _siWave);
    u8g2.drawStr(0, 8, hdr);
    u8g2.drawHLine(0, 10, 128);
    // Player
    u8g2.drawBox(_siPx-5, SI_PY, 11, 2);
    u8g2.drawBox(_siPx-2, SI_PY-2, 5, 2);
    u8g2.drawPixel(_siPx, SI_PY-4);
    // Invaders with drop offset
    for (uint8_t i = 0; i < SI_INV_CNT; i++) {
        if (!_siInvAlive[i]) continue;
        int8_t col = i % SI_INV_COLS, row = i / SI_INV_COLS;
        int ix = SI_GRID_X0 + col*SI_GCELL_W + _siInvOffX;
        int iy = SI_GRID_Y0 + row*SI_GCELL_H + _siInvDropY;
        _siDrawInvader(ix, iy, alt);
    }
    for (uint8_t i = 0; i < SI_BULLET_CNT; i++) {
        if (!_siBullets[i].active) continue;
        u8g2.drawBox(_siBullets[i].x, _siBullets[i].y, 2, 4);
    }
    if (_siPaused) {
        u8g2.drawBox(35,25,58,14); u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_6x10_tr); u8g2.drawStr(40,35,"PAUSED"); u8g2.setDrawColor(1);
    }
    u8g2.sendBuffer();
}

static void _siDrawGameOver(bool won) {
    uint16_t best = uiHighScoreGet("si");
    u8g2.clearBuffer();
    _appTitle("Space Invaders");
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.drawStr(won ? 18 : 10, 36, won ? "YOU WIN!" : "GAME OVER");
    u8g2.setFont(u8g2_font_6x10_tr);
    char sc[28]; snprintf(sc, 28, "Score:%d  Best:%d", _siScore, best);
    u8g2.drawStr(4, 52, sc);
    _appHint("Sel=replay  BACK=exit");
    u8g2.sendBuffer();
}

static void spaceInvadersRun() {
    _siReset(true);
    randomSeed(analogRead(A0) ^ millis());
    inputSettle(200);
    bool dirty = true, gameOver = false, won = false;

    auto invInterval = [&]() -> uint32_t {
        uint32_t base = (uint32_t)constrain(800 - (int)_siWave*80, 150, 800);
        return base * _siInvAliveN / SI_INV_CNT + 100;
    };

    while (true) {
        inputPoll();
        uiTouchActivity();
        if (inputBackHeld()) { inputSettle(150); return; }

        if (gameOver) {
            if (dirty) { dirty = false; _siDrawGameOver(won); }
            if (inputSelectOrLearnFired()) { _siReset(true); gameOver = false; won = false; dirty = true; }
            if (inputBackFired()) { inputSettle(150); return; }
            delay(10); continue;
        }

        // Player movement — accelerated
        int8_t s = inputConsumeScrollFast();
        if (s) { _siPx = (int8_t)constrain((int)_siPx + s*_siPSpeed, 5, SI_W-6); dirty = true; }
        if (inputLearnFired()) { _siPSpeed = (uint8_t)(_siPSpeed % 4 + 1); }
        if (inputSelectFired()) { _siPaused = !_siPaused; dirty = true; }
        if (inputBackFired()) { inputSettle(150); return; }

        if (!_siPaused) {
            uint32_t now = millis();

            // Player shoot
            if (_siShootCool == 0 && inputSelectHeld()) {
                for (uint8_t i = 0; i < SI_BULLET_CNT; i++) {
                    if (!_siBullets[i].active) {
                        _siBullets[i] = { _siPx, (int8_t)(SI_PY-5), true, true };
                        _siShootCool = 10; break;
                    }
                }
            }
            if (_siShootCool > 0) _siShootCool--;

            // Invader march
            if (now - _siInvMoveMs >= invInterval()) {
                _siInvMoveMs = now;
                bool hitEdge = false;
                for (uint8_t i = 0; i < SI_INV_CNT; i++) {
                    if (!_siInvAlive[i]) continue;
                    int8_t col = i % SI_INV_COLS;
                    int ix = SI_GRID_X0 + col*SI_GCELL_W + _siInvOffX + _siInvDir*3;
                    if (ix <= 0 || ix+5 >= SI_W) { hitEdge = true; break; }
                }
                if (hitEdge) {
                    _siInvDir = -_siInvDir;
                    _siInvDropY += 4;
                    // Check if invaders reached player line
                    for (uint8_t i = 0; i < SI_INV_CNT; i++) {
                        if (!_siInvAlive[i]) continue;
                        int8_t row = i / SI_INV_COLS;
                        if (SI_GRID_Y0 + row*SI_GCELL_H + _siInvDropY + 8 >= SI_PY-4) {
                            _siLives = 0; break;
                        }
                    }
                } else {
                    _siInvOffX += _siInvDir*3;
                }
                dirty = true;
            }

            // Enemy fire
            if (now - _siInvFireMs >= (uint32_t)max(400, 1200 - (int)_siWave*100) && _siInvAliveN) {
                _siInvFireMs = now;
                uint8_t pick = (uint8_t)(random(0, SI_INV_CNT));
                for (uint8_t t = 0; t < SI_INV_CNT; t++) {
                    uint8_t idx = (pick+t) % SI_INV_CNT;
                    if (_siInvAlive[idx]) {
                        int8_t col = idx%SI_INV_COLS, row = idx/SI_INV_COLS;
                        int8_t bx = (int8_t)(SI_GRID_X0+col*SI_GCELL_W+_siInvOffX+2);
                        int8_t by = (int8_t)(SI_GRID_Y0+row*SI_GCELL_H+_siInvDropY+5);
                        for (uint8_t bi = 0; bi < SI_BULLET_CNT; bi++) {
                            if (!_siBullets[bi].active) { _siBullets[bi]={bx,by,true,false}; break; }
                        }
                        break;
                    }
                }
            }

            // Bullet movement
            if (now - _siLastMoveMs >= 40) {
                _siLastMoveMs = now;
                for (uint8_t i = 0; i < SI_BULLET_CNT; i++) {
                    if (!_siBullets[i].active) continue;
                    if (_siBullets[i].player) {
                        _siBullets[i].y -= 3;
                        if (_siBullets[i].y < 11) { _siBullets[i].active = false; continue; }
                        for (uint8_t j = 0; j < SI_INV_CNT; j++) {
                            if (!_siInvAlive[j]) continue;
                            int8_t col = j%SI_INV_COLS, row = j/SI_INV_COLS;
                            int ix = SI_GRID_X0+col*SI_GCELL_W+_siInvOffX;
                            int iy = SI_GRID_Y0+row*SI_GCELL_H+_siInvDropY;
                            if (_siBullets[i].x>=ix && _siBullets[i].x<=ix+5 &&
                                _siBullets[i].y>=iy && _siBullets[i].y<=iy+4) {
                                _siInvAlive[j]=false; _siInvAliveN--;
                                _siScore += 10*_siWave; _siBullets[i].active=false;
                                if (_siInvAliveN==0) {
                                    uiHighScoreSet("si", _siScore);
                                    if (++_siWave > 5) { won=true; gameOver=true; }
                                    else _siReset(false);
                                }
                                break;
                            }
                        }
                    } else {
                        _siBullets[i].y += 2;
                        if (_siBullets[i].y > SI_PY+2) { _siBullets[i].active=false; continue; }
                        if (abs(_siBullets[i].x - _siPx) <= 5 && _siBullets[i].y >= SI_PY-2) {
                            _siBullets[i].active=false; _siLives--;
                            if (!_siLives) { uiHighScoreSet("si",_siScore); gameOver=true; won=false; }
                        }
                    }
                }
                dirty = true;
            }
            if (!_siLives && !gameOver) { uiHighScoreSet("si",_siScore); gameOver=true; }
        }
        if (dirty) { dirty=false; _siDraw(); }
        delay(5);
    }
}
