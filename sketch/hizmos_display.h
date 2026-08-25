#pragma once

#include "hizmos_arm_compat.h"
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "hizmos_fonts.h"

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

inline void dispClear() { u8g2.clearBuffer(); }
inline void dispSend()  { u8g2.sendBuffer(); }

inline void drawSplash() {
    u8g2.clearBuffer();
    u8g2.setFontMode(1);
    u8g2.setFont(HIZ_FONT_SPLASH_BIG);
    u8g2.drawStr(8, 28, "HermesQ");
    u8g2.setFont(HIZ_FONT_SPLASH_MID);
    u8g2.drawStr(22, 48, "multitool");
    u8g2.sendBuffer();
}

inline void drawLoading() {
    u8g2.clearBuffer();
    u8g2.setFont(HIZ_FONT_LOADING);
    u8g2.drawStr(10, 42, "loading...");
    u8g2.sendBuffer();
}

inline void drawList(const char* const names[], uint8_t cnt, int8_t sel, const char* title) {
    u8g2.clearBuffer();
    u8g2.setFont(HIZ_FONT_BODY);
    u8g2.drawStr(0, 10, title);
    u8g2.drawHLine(0, 12, 128);

    if (cnt == 0) {
        u8g2.drawStr(4, 30, "(empty)");
    } else {
        int8_t top = sel - 1;
        if (top < 0) top = 0;
        if (top + 3 > (int)cnt) top = (cnt > 3) ? (int8_t)(cnt - 3) : 0;

        for (uint8_t i = 0; i < 3; i++) {
            int8_t idx = top + (int8_t)i;
            if (idx >= (int8_t)cnt) break;
            int y = 26 + (int)i * 13;
            if (idx == sel) {
                u8g2.drawRBox(0, y - 10, 124, 12, 2);
                u8g2.setDrawColor(0);
                u8g2.drawStr(4, y, names[idx]);
                u8g2.setDrawColor(1);
            } else {
                u8g2.drawStr(4, y, names[idx]);
            }
        }
    }

    u8g2.sendBuffer();
}
