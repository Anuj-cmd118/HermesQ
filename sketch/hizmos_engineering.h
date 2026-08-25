#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_engineering.h  —  Engineering Calculators (QoL rev)
//
//  QoL improvements:
//    • LEARN (hold) opens step-size picker via uiPickStep()
//    • inputConsumeScrollFast() for field value adjustment
//    • uiToast() confirms result saved / bad input
//    • uiConfirm() before clearing inputs
//    • hold-BACK exits from any depth
//    • Shared EngField widget uses uiDrawTitle/uiDrawHint
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <math.h>
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_ui.h"

// ─── EngField widget ──────────────────────────────────────────
struct EngField {
    const char* label;
    float       value;
    float       step;
    float       minV;
    float       maxV;
    uint8_t     decimals;
};

static void _engDrawFields(const char* title, EngField* f, uint8_t cnt,
                            int8_t cur, const char* result = nullptr) {
    u8g2.clearBuffer();
    uiDrawTitle(title);
    u8g2.setFont(u8g2_font_6x10_tr);
    uint8_t show = min(cnt, (uint8_t)3);
    uint8_t top  = (uint8_t)constrain((int)cur - 1, 0, (int)cnt - (int)show);
    for (uint8_t i = 0; i < show && (top+i) < cnt; i++) {
        uint8_t idx = top + i;
        int y = 25 + (int)i * 13;
        bool sel = (idx == (uint8_t)cur);
        char line[24];
        if (f[idx].decimals == 0)      snprintf(line, 24, "%-9s %7.0f", f[idx].label, f[idx].value);
        else if (f[idx].decimals == 1) snprintf(line, 24, "%-9s %7.1f", f[idx].label, f[idx].value);
        else                            snprintf(line, 24, "%-9s %7.2f", f[idx].label, f[idx].value);
        if (sel) { u8g2.drawRBox(0, y-10, 128, 12, 2); u8g2.setDrawColor(0); }
        u8g2.drawStr(2, y, line);
        if (sel) u8g2.setDrawColor(1);
    }
    if (result) { u8g2.setFont(u8g2_font_5x7_tr); u8g2.drawStr(0, 53, result); }
    uiDrawHint(result ? "Lrn=redo BACK=exit" : "Rot=val Sel=nxt Lrn(hld)=step");
    u8g2.sendBuffer();
}

// Step presets used by uiPickStep
static const float _engStepSmall[] = { 0.001f, 0.01f, 0.1f, 0.5f, 1.0f };
static const float _engStepMed[]   = { 0.1f,   0.5f,  1.0f, 5.0f, 10.0f };
static const float _engStepLarge[] = { 1.0f,  10.0f, 50.0f, 100.0f, 1000.0f };

// Returns false if user pressed BACK to exit entirely
static bool _engSpinFields(const char* title, EngField* f, uint8_t cnt) {
    int8_t cur = 0;
    bool dirty = true;
    inputSettle(150);
    while (true) {
        inputPoll();
        uiTouchActivity();
        if (inputBackHeld()) return false;   // exit app
        if (dirty) { dirty = false; _engDrawFields(title, f, cnt, cur); }

        int8_t s = inputConsumeScrollFast();
        if (s) {
            f[cur].value = constrain(f[cur].value + s * f[cur].step, f[cur].minV, f[cur].maxV);
            dirty = true;
        }
        if (inputSelectFired()) {
            cur++;
            if (cur >= (int8_t)cnt) return true;   // all fields done → compute
            dirty = true;
        }
        // Hold-LEARN → step-size picker for current field
        if (inputLearnHeld()) {
            const float* steps = (f[cur].step < 0.1f)  ? _engStepSmall :
                                  (f[cur].step < 10.0f) ? _engStepMed   : _engStepLarge;
            uint8_t n = (f[cur].step < 0.1f) ? 5 :
                        (f[cur].step < 10.0f) ? 5 : 5;
            f[cur].step = uiPickStep(steps, n, f[cur].step);
            dirty = true;
        }
        if (inputBackFired()) {
            if (cur > 0) { cur--; dirty = true; }
            else { inputSettle(150); return false; }
        }
        delay(5);
    }
}

static void _engShowResult(const char* title, EngField* f, uint8_t cnt, const char* res) {
    bool dirty = true;
    while (true) {
        inputPoll();
        uiTouchActivity();
        if (dirty) { dirty = false; _engDrawFields(title, f, cnt, -1, res); }
        if (inputLearnFired() || inputBackFired() || inputBackHeld()) return;
        delay(5);
    }
}

// ─── 1. Ohm's Law ─────────────────────────────────────────────
static void _calcOhmsLaw() {
    const char* choices[] = { "V & I", "V & R", "I & R" };
    int8_t pick = 0; bool dirty = true;
    inputSettle(150);
    while (true) {
        inputPoll(); uiTouchActivity();
        if (inputBackHeld()) return;
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer(); uiDrawTitle("Ohm's Law");
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 24, "Known values:");
            for (int i = 0; i < 3; i++) {
                bool s = (i == pick); int y = 36 + i*10;
                if (s) { u8g2.drawRBox(0,y-8,90,10,2); u8g2.setDrawColor(0); }
                u8g2.drawStr(4, y, choices[i]);
                if (s) u8g2.setDrawColor(1);
            }
            uiDrawHint("Rot=pick  Sel=confirm");
            u8g2.sendBuffer();
        }
        int8_t s = inputConsumeScroll();
        if (s) { pick = (int8_t)((pick+s+3)%3); dirty = true; }
        if (inputSelectOrLearnFired()) break;
        if (inputBackFired()) { inputSettle(150); return; }
        delay(5);
    }
    char result[32] = "";
    if (pick == 0) {
        EngField f[] = {{"V (V)", 5.0f,0.1f,0,1000,2},{"I (mA)",100.0f,1,0.001f,50000,0}};
        if (!_engSpinFields("Ohm: V & I", f, 2)) return;
        float V=f[0].value, I=f[1].value/1000.0f, R=(I>0)?V/I:0, P=V*I;
        snprintf(result, 32, "R=%.1f\xf4  P=%.3fW", R, P);
    } else if (pick == 1) {
        EngField f[] = {{"V (V)",5.0f,0.1f,0,1000,2},{"R (\xf4)",50.0f,10,0.1f,1e6f,0}};
        if (!_engSpinFields("Ohm: V & R", f, 2)) return;
        float V=f[0].value, R=f[1].value, I=(R>0)?V/R:0, P=V*I;
        snprintf(result, 32, "I=%.1fmA P=%.3fW", I*1000, P);
    } else {
        EngField f[] = {{"I (mA)",100.0f,1,0.001f,50000,0},{"R (\xf4)",50.0f,10,0.1f,1e6f,0}};
        if (!_engSpinFields("Ohm: I & R", f, 2)) return;
        float I=f[0].value/1000.0f, R=f[1].value, V=I*R, P=V*I;
        snprintf(result, 32, "V=%.2fV  P=%.3fW", V, P);
    }
    EngField d[1]={{"",0,0,0,0,0}};
    _engShowResult("Ohm's Law", d, 0, result);
}

// ─── 2. LED Resistor ──────────────────────────────────────────
static void _calcLedResistor() {
    EngField f[] = {{"Vs (V)",5.0f,0.1f,1,30,1},{"Vf (V)",2.0f,0.05f,0.5f,5,2},{"If (mA)",20.0f,1,1,500,0}};
    inputSettle(150);
    if (!_engSpinFields("LED Resistor", f, 3)) return;
    float Vs=f[0].value, Vf=f[1].value, If=f[2].value/1000.0f;
    float R=(Vs-Vf)/If;
    // Nearest E12
    const float e12[]={1,1.2f,1.5f,1.8f,2.2f,2.7f,3.3f,3.9f,4.7f,5.6f,6.8f,8.2f};
    float best=10,decade=1,tmp=R;
    while(tmp>=100){tmp/=10;decade*=10;} while(tmp<10){tmp*=10;decade/=10;}
    for(uint8_t i=0;i<12;i++) if(fabsf(e12[i]*decade-R)<fabsf(best-R)) best=e12[i]*decade;
    char res[32]; snprintf(res,32,"R=%.0f\xf4 (E12:%.0f\xf4)",R,best);
    EngField d[1]={{"",0,0,0,0,0}}; _engShowResult("LED Resistor",d,0,res);
}

// ─── 3. Voltage Divider ───────────────────────────────────────
static void _calcVoltageDivider() {
    EngField f[] = {{"Vin (V)",12,0.5f,0.1f,240,1},{"R1 (k\xf4)",10,1,0.1f,1000,1},{"R2 (k\xf4)",5,1,0.1f,1000,1}};
    inputSettle(150);
    if (!_engSpinFields("Volt Divider", f, 3)) return;
    float Vin=f[0].value, R1=f[1].value*1000, R2=f[2].value*1000;
    float Vout=Vin*R2/(R1+R2), ratio=R2/(R1+R2);
    char res[32]; snprintf(res,32,"Vout=%.2fV (%.0f%%)",Vout,ratio*100);
    EngField d[1]={{"",0,0,0,0,0}}; _engShowResult("Volt Divider",d,0,res);
}

// ─── 4. Battery Runtime ───────────────────────────────────────
static void _calcBatteryRuntime() {
    EngField f[] = {{"Cap (mAh)",3000,50,10,50000,0},{"Load (mA)",200,10,1,10000,0}};
    inputSettle(150);
    if (!_engSpinFields("Batt Runtime", f, 2)) return;
    float hrs = (f[1].value>0) ? f[0].value/f[1].value : 0;
    char res[32]; snprintf(res,32,"Runtime: %dh %dm",(int)hrs,(int)((hrs-(int)hrs)*60));
    EngField d[1]={{"",0,0,0,0,0}}; _engShowResult("Batt Runtime",d,0,res);
}

// ─── 5. UART Baud ─────────────────────────────────────────────
static void _calcUartBaud() {
    static const uint32_t bds[]={1200,2400,4800,9600,19200,38400,57600,115200,230400,460800,921600};
    EngField f[] = {{"Clk (MHz)",16,1,1,480,0}};
    inputSettle(150);
    if (!_engSpinFields("UART Baud", f, 1)) return;
    float clkHz = f[0].value * 1e6f;
    char result[40] = "";
    char tmp[14];
    size_t resLen = 0;
    for (uint8_t i=0;i<4;i++) {
        uint32_t div=(uint32_t)(clkHz/(16.0f*bds[i])+0.5f);
        int n = snprintf(tmp,14,"%lu@%lk ",(unsigned long)div,(unsigned long)bds[i]/1000);
        // Bounded append without strncat (Zephyr minimal libc lacks strncat)
        if (n > 0) {
            size_t avail = (sizeof(result) - 1) - resLen;
            size_t copyLen = ((size_t)n < avail) ? (size_t)n : avail;
            memcpy(result + resLen, tmp, copyLen);
            resLen += copyLen;
            result[resLen] = '\0';
        }
    }
    EngField d[1]={{"",0,0,0,0,0}}; _engShowResult("UART Baud",d,0,result);
}

// ─── 6. PWM Frequency ─────────────────────────────────────────
static void _calcPwmFreq() {
    EngField f[] = {{"Clk (MHz)",72,1,1,480,0},{"Prescaler",72,1,1,65536,0},{"Period",1000,10,1,65536,0}};
    inputSettle(150);
    if (!_engSpinFields("PWM Freq", f, 3)) return;
    float freq = f[0].value*1e6f / (f[1].value * f[2].value);
    char res[32];
    if (freq>=1000) snprintf(res,32,"f = %.2f kHz",freq/1000);
    else            snprintf(res,32,"f = %.1f Hz",freq);
    EngField d[1]={{"",0,0,0,0,0}}; _engShowResult("PWM Freq",d,0,res);
}

// ─── 7. RF Wavelength ─────────────────────────────────────────
static void _calcRfWavelength() {
    EngField f[] = {{"Freq(MHz)",433.92f,0.1f,0.1f,6000,2}};
    inputSettle(150);
    if (!_engSpinFields("RF Wavelength", f, 1)) return;
    float lam = 300.0f/f[0].value;
    char res[40]; snprintf(res,40,"L=%.2fm H=%.2fm Q=%.2fm",lam,lam/2,lam/4);
    EngField d[1]={{"",0,0,0,0,0}}; _engShowResult("RF Wavelength",d,0,res);
}

// ─── 8. Quarter-Wave Stub ─────────────────────────────────────
static void _calcQuarterWave() {
    EngField f[] = {{"Freq(MHz)",433.92f,0.1f,0.1f,6000,2},{"VF(x0.01)",66,1,10,99,0}};
    inputSettle(150);
    if (!_engSpinFields("Quarter-Wave", f, 2)) return;
    float L_mm = (3e8f*(f[1].value/100.0f)) / (4.0f*f[0].value*1e6f) * 1000.0f;
    char res[32]; snprintf(res,32,"Length = %.1f mm",L_mm);
    EngField d[1]={{"",0,0,0,0,0}}; _engShowResult("Quarter-Wave",d,0,res);
}

// ─── Public dispatcher ────────────────────────────────────────
static void engineeringAppRun(uint8_t idx) {
    inputSettle(200);
    switch(idx) {
        case 0: _calcOhmsLaw();        break;
        case 1: _calcLedResistor();    break;
        case 2: _calcVoltageDivider(); break;
        case 3: _calcBatteryRuntime(); break;
        case 4: _calcUartBaud();       break;
        case 5: _calcPwmFreq();        break;
        case 6: _calcRfWavelength();   break;
        case 7: _calcQuarterWave();    break;
    }
}
