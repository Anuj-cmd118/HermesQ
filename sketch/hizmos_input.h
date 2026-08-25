#pragma once

#include <Arduino.h>

#define ENC_CLK     6
#define ENC_DT      7
#define ENC_SW      8
#define BTN_BACK    9
#define BTN_LEARN   4

// ─── Public API ───────────────────────────────────────────────
void    inputInit();
void    inputPoll();
void    inputSettle(uint32_t graceMs);
bool    inputSelectFired();
bool    inputBackFired();
bool    inputLearnFired();
bool    inputSelectOrLearnFired();
bool    inputSelectPending();          // non-destructive peek at pending SELECT
bool    inputBackPending();            // non-destructive peek at pending BACK
bool    inputLearnPending();           // non-destructive peek at pending LEARN
bool    inputScrollPending();          // non-destructive: true if a step is waiting (does not consume)
void    inputDiscardScroll();          // hard-clear any pending scroll (use in non-scrolling states)
int8_t  inputConsumeScroll();          // 0, +1, or -1 per call
int8_t  inputConsumeScrollFast();      // accelerated: up to ±3 per call
bool    inputSelectHeld();             // true while SELECT held ≥ 600ms
bool    inputBackHeld();               // true while BACK held  ≥ 800ms (home)
bool    inputLearnHeld();              // true while LEARN held ≥ 600ms

#ifdef HIZMOS_INPUT_IMPL

// ─── Encoder state ────────────────────────────────────────────
static int8_t   encDelta     = 0;
static uint32_t lastScrollMs = 0;
static uint32_t inputReadyMs = 0;

// Acceleration: track how quickly pulses arrive
static uint32_t lastPulseMs  = 0;
static uint8_t  pulseRate    = 0;   // pulses in last 300ms window

// Quadrature decode state. We sample BOTH encoder lines every poll and
// only register a step once a full, valid 4-phase Gray-code cycle
// completes (qAccum reaches +-4). This is the standard robust technique
// for noisy/cheap encoders: a real human-driven rotation always walks
// through 00->01->11->10->00 (or the reverse) in order, while electrical
// noise or contact bounce on a single line produces transitions that
// don't fit that sequence — QUAD_TABLE maps those invalid jumps to 0, so
// they're silently ignored instead of being miscounted as movement.
// This is what was actually causing the "scrolls by itself" behavior:
// the old code counted every CLK edge directly, so any glitch on that
// one line (bounce, or EM pickup from the nearby radio modules) was
// indistinguishable from a real turn.
static uint8_t  qState  = 0;
static int8_t   qAccum  = 0;
static const int8_t QUAD_TABLE[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

// ─── Button state ─────────────────────────────────────────────
static const uint32_t DEBOUNCE_MS     = 30;
static const uint32_t HOLD_SW_MS      = 600;
static const uint32_t HOLD_BACK_MS    = 800;
static const uint32_t HOLD_LEARN_MS   = 600;

struct BtnState {
    uint8_t  pin;
    bool     down;
    bool     fired;
    bool     heldFired;
    uint32_t downAt;
};

static BtnState _bSW    = { ENC_SW,    false, false, false, 0 };
static BtnState _bBack  = { BTN_BACK,  false, false, false, 0 };
static BtnState _bLearn = { BTN_LEARN, false, false, false, 0 };

static void _pollBtn(BtnState& b, uint32_t holdMs) {
    bool pressed = (digitalRead(b.pin) == LOW);
    if (pressed && !b.down)  { b.downAt = millis(); b.down = true; }
    if (!pressed && b.down)  {
        if (millis() - b.downAt >= DEBOUNCE_MS && !b.heldFired) b.fired = true;
        b.down = false; b.heldFired = false;
    }
}

static inline void pollEncoder() {
    uint8_t a  = (digitalRead(ENC_CLK) == HIGH) ? 1 : 0;
    uint8_t b  = (digitalRead(ENC_DT)  == HIGH) ? 1 : 0;
    uint8_t ab = (uint8_t)((a << 1) | b);

    qState = (uint8_t)(((qState << 2) | ab) & 0x0F);
    qAccum = (int8_t)(qAccum + QUAD_TABLE[qState]);

    if (qAccum >= 4 || qAccum <= -4) {
        int8_t dir = (qAccum > 0) ? +1 : -1;
        qAccum = 0;
        encDelta += dir;
        if (encDelta >  8) encDelta =  8;
        if (encDelta < -8) encDelta = -8;
        // Track pulse rate for acceleration
        uint32_t now = millis();
        if (now - lastPulseMs < 300) {
            if (pulseRate < 10) pulseRate++;
        } else {
            pulseRate = 0;
        }
        lastPulseMs = now;
    }
}

static inline void pollButtons() {
    _pollBtn(_bSW,    HOLD_SW_MS);
    _pollBtn(_bBack,  HOLD_BACK_MS);
    _pollBtn(_bLearn, HOLD_LEARN_MS);
}

inline void inputSettle(uint32_t graceMs) {
    inputReadyMs = millis() + graceMs;
    encDelta     = 0;
    qAccum       = 0;
    pulseRate    = 0;
    _bSW.fired   = _bBack.fired   = _bLearn.fired   = false;
    _bSW.heldFired = _bBack.heldFired = _bLearn.heldFired = false;
    // Drain any physical bounce — shorter than before (50ms vs 100ms)
    for (uint8_t i = 0; i < 10; i++) { pollEncoder(); delay(5); }
    encDelta = 0;
    qAccum   = 0;
}

inline void inputInit() {
    pinMode(ENC_CLK,   INPUT_PULLUP);
    pinMode(ENC_DT,    INPUT_PULLUP);
    pinMode(ENC_SW,    INPUT_PULLUP);
    pinMode(BTN_BACK,  INPUT_PULLUP);
    pinMode(BTN_LEARN, INPUT_PULLUP);
    // Seed quadrature state from the encoder's actual resting position so
    // the very first poll doesn't see a false "transition" from 0.
    uint8_t a = (digitalRead(ENC_CLK) == HIGH) ? 1 : 0;
    uint8_t b = (digitalRead(ENC_DT)  == HIGH) ? 1 : 0;
    qState = (uint8_t)((a << 1) | b);
    inputSettle(600);
}

inline void inputPoll() {
    pollEncoder();
    pollButtons();
}

inline bool inputSelectFired() {
    bool v = _bSW.fired; _bSW.fired = false; return v;
}
inline bool inputBackFired() {
    bool v = _bBack.fired; _bBack.fired = false; return v;
}
inline bool inputLearnFired() {
    bool v = _bLearn.fired; _bLearn.fired = false; return v;
}
inline bool inputSelectOrLearnFired() {
    bool v = _bSW.fired || _bLearn.fired;
    _bSW.fired = false; _bLearn.fired = false; return v;
}

// Non-consuming peeks — use for screen-saver wake checks that must not
// steal events from the state machine / app handlers below.
inline bool inputSelectPending() { return _bSW.fired; }
inline bool inputBackPending()   { return _bBack.fired; }
inline bool inputLearnPending()  { return _bLearn.fired; }

// Non-destructive peek: is a scroll step waiting? Does NOT touch encDelta
// or lastScrollMs, so it's safe to call from places (like a generic
// "wake the screen" check) that shouldn't compete with the real consumer.
inline bool inputScrollPending() {
    if (millis() < inputReadyMs) return false;
    return encDelta != 0;
}

// Hard-discard any accumulated scroll. Call this from screens/states that
// don't navigate via the encoder, so stray noise can't quietly bank up
// while idle and then dump out as a burst the next time a scrolling
// screen becomes active.
inline void inputDiscardScroll() {
    encDelta = 0;
}

// Hold detection — non-consuming (cleared by caller if needed)
inline bool inputSelectHeld() {
    if (_bSW.down && !_bSW.heldFired && millis() - _bSW.downAt >= HOLD_SW_MS) {
        _bSW.heldFired = true; return true;
    }
    return false;
}
inline bool inputBackHeld() {
    if (_bBack.down && !_bBack.heldFired && millis() - _bBack.downAt >= HOLD_BACK_MS) {
        _bBack.heldFired = true; return true;
    }
    return false;
}
inline bool inputLearnHeld() {
    if (_bLearn.down && !_bLearn.heldFired && millis() - _bLearn.downAt >= HOLD_LEARN_MS) {
        _bLearn.heldFired = true; return true;
    }
    return false;
}

// Standard scroll — 1 step, 80ms cooldown (was 140ms)
inline int8_t inputConsumeScroll() {
    if (millis() < inputReadyMs) { encDelta = 0; return 0; }
    if (encDelta == 0) return 0;
    if (millis() - lastScrollMs < 80) return 0;
    int8_t step = (encDelta > 0) ? 1 : -1;
    encDelta -= step;
    lastScrollMs = millis();
    return step;
}

// Accelerated scroll — up to ±3 steps when spinning fast
inline int8_t inputConsumeScrollFast() {
    if (millis() < inputReadyMs) { encDelta = 0; return 0; }
    if (encDelta == 0) return 0;
    uint32_t cooldown = (pulseRate > 5) ? 40 : 80;
    if (millis() - lastScrollMs < cooldown) return 0;
    int8_t mag   = (pulseRate > 5) ? 3 : (pulseRate > 2) ? 2 : 1;
    int8_t step  = (encDelta > 0) ? mag : -mag;
    encDelta = 0;
    lastScrollMs = millis();
    return step;
}
#endif
