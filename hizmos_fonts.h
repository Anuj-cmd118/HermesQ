#pragma once

// Arduino Uno Q (arduino:zephyr:unoq) ships U8g2 with a reduced font set.
// Use only fonts that compile on the STM32/Zephyr core (see IR Remote Hub reference).
#ifndef HIZ_FONT_TINY
#define HIZ_FONT_TINY       u8g2_font_6x10_tr
#endif
#ifndef HIZ_FONT_BODY
#define HIZ_FONT_BODY       u8g2_font_6x10_tr
#endif
#ifndef HIZ_FONT_HINT
#define HIZ_FONT_HINT       u8g2_font_6x10_tr
#endif
#ifndef HIZ_FONT_TITLE
#define HIZ_FONT_TITLE      u8g2_font_10x20_tr
#endif
#ifndef HIZ_FONT_MENU
#define HIZ_FONT_MENU       u8g2_font_6x10_tr
#endif
#ifndef HIZ_FONT_MENU_SEL
#define HIZ_FONT_MENU_SEL   u8g2_font_6x10_tr
#endif
#ifndef HIZ_FONT_LOADING
#define HIZ_FONT_LOADING    u8g2_font_10x20_tr
#endif
#ifndef HIZ_FONT_SPLASH_BIG
#define HIZ_FONT_SPLASH_BIG u8g2_font_10x20_tr
#endif
#ifndef HIZ_FONT_SPLASH_MID
#define HIZ_FONT_SPLASH_MID u8g2_font_6x10_tr
#endif
