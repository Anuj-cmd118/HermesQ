#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_notes.h  —  Notes App for HermesQ
//
//  All notes stored in Python SQLite via Bridge RPC.
//
//  Bridge RPC used (registered in notes_main.py):
//    notes_list      ()                  → "id:title|id:title|…"
//    notes_read      (id)                → "title\nbody line1\nline2…"
//    notes_new       (title:body)        → "ok" | "err:…"
//    notes_delete    (id)                → "ok" | "err:…"
//    notes_search    (query)             → "id:title|…"
//
//  Entry points:
//    notesRun()   — main notes app (browse / create / search)
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_fonts.h"
#include "hizmos_ui.h"

// ─────────────────────────────────────────────────────────────
//  Local list cache
// ─────────────────────────────────────────────────────────────
#define NOTES_MAX   20
#define NOTES_ID_LEN  8
#define NOTES_TL_LEN 20

struct NoteEntry {
    char id[NOTES_ID_LEN];
    char title[NOTES_TL_LEN];
};

static NoteEntry _notesList[NOTES_MAX];
static uint8_t   _notesCnt = 0;
static int8_t    _notesSel = 0;

// ─────────────────────────────────────────────────────────────
//  Shared helpers
// ─────────────────────────────────────────────────────────────
static void _notTitle(const char* t) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 10, t);
    u8g2.drawHLine(0, 12, 128);
}
static void _notHint(const char* h) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawHLine(0, 55, 128);
    u8g2.drawStr(0, 63, h);
}

// Parse "id:title|id:title|…" into _notesList
static void _notParseList(const String& raw) {
    _notesCnt = 0;
    int pos = 0;
    while (pos < (int)raw.length() && _notesCnt < NOTES_MAX) {
        int pipe  = raw.indexOf('|', pos);
        int end   = (pipe == -1) ? (int)raw.length() : pipe;
        String tok = raw.substring(pos, end);
        int colon = tok.indexOf(':');
        if (colon > 0) {
            tok.substring(0, colon).toCharArray(_notesList[_notesCnt].id, NOTES_ID_LEN);
            tok.substring(colon + 1).toCharArray(_notesList[_notesCnt].title, NOTES_TL_LEN);
            _notesCnt++;
        }
        pos = (pipe == -1) ? (int)raw.length() : pipe + 1;
    }
}

static void _notLoadList(const char* query = nullptr) {
    String resp = "";
    if (query && strlen(query) > 0) Bridge.call("notes_search", query).result(resp);
    else                             Bridge.call("notes_list").result(resp);
    _notParseList(resp);
    if (_notesSel >= (int8_t)_notesCnt) _notesSel = (_notesCnt > 0) ? (int8_t)(_notesCnt - 1) : 0;
}

// ─────────────────────────────────────────────────────────────
//  Draw list
// ─────────────────────────────────────────────────────────────
static void _notDrawList(const char* title) {
    u8g2.clearBuffer();
    _notTitle(title);

    if (_notesCnt == 0) {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(4, 32, "(no notes)");
    } else {
        int8_t top = _notesSel - 1;
        if (top < 0)                        top = 0;
        if (top + 3 > (int8_t)_notesCnt)   top = (_notesCnt > 3) ? (int8_t)(_notesCnt - 3) : 0;

        u8g2.setFont(u8g2_font_6x10_tr);
        for (uint8_t i = 0; i < 3; i++) {
            int8_t idx = top + (int8_t)i;
            if (idx >= (int8_t)_notesCnt) break;
            int y   = 26 + (int)i * 13;
            bool sel = (idx == _notesSel);
            if (sel) {
                u8g2.drawRBox(0, y - 10, 124, 12, 2);
                u8g2.setDrawColor(0);
                u8g2.drawStr(4, y, _notesList[idx].title);
                u8g2.setDrawColor(1);
            } else {
                u8g2.drawStr(4, y, _notesList[idx].title);
            }
        }
        u8g2.setFont(u8g2_font_5x7_tr);
        if (top > 0)                          u8g2.drawStr(120, 20, "^");
        if (top + 3 < (int8_t)_notesCnt)     u8g2.drawStr(120, 53, "v");
        char sc[8]; snprintf(sc, 8, "%d/%d", _notesSel + 1, _notesCnt);
        u8g2.drawStr(86, 53, sc);
    }
    _notHint("Sel=open Lrn=del BACK=up");
    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────────
//  Read / view note
// ─────────────────────────────────────────────────────────────
static void _notViewNote(const char* id) {
    String resp = "";
    Bridge.call("notes_read", id).result(resp);

    // resp = "title\nline1\nline2\n…"
    // Extract lines for paging
    static char lines[8][22];
    uint8_t lineCnt = 0;
    int pos = 0;
    while (pos < (int)resp.length() && lineCnt < 8) {
        int nl = resp.indexOf('\n', pos);
        int end = (nl == -1) ? (int)resp.length() : nl;
        resp.substring(pos, min(end, pos + 21)).toCharArray(lines[lineCnt++], 22);
        pos = (nl == -1) ? (int)resp.length() : nl + 1;
    }

    int8_t page = 0;
    int8_t pages = max(1, (int)ceil(lineCnt / 3.0f));
    bool dirty = true;
    inputSettle(150);

    while (true) {
        inputPoll();
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer();
            _notTitle(lineCnt > 0 ? lines[0] : "(empty)");
            u8g2.setFont(u8g2_font_5x7_tr);
            for (uint8_t i = 0; i < 3; i++) {
                uint8_t li = 1 + page * 3 + i;
                if (li >= lineCnt) break;
                u8g2.drawStr(0, 24 + i * 10, lines[li]);
            }
            char pg[12]; snprintf(pg, 12, "p%d/%d", page + 1, pages);
            _notHint(pg);
            u8g2.sendBuffer();
        }

        int8_t s = inputConsumeScrollFast(); uiTouchActivity(); if(inputBackHeld()){inputSettle(150);return;}
        if (s) {
            page = (int8_t)constrain((int)page + s, 0, pages - 1);
            dirty = true;
        }
        if (inputBackFired()) { inputSettle(150); return; }
        if (inputSelectOrLearnFired()) { inputSettle(150); return; }
        delay(5);
    }
}

// ─────────────────────────────────────────────────────────────
//  New note — character-by-character entry
//  Encoder scrolls A-Z 0-9 space; SELECT appends; BACK deletes;
//  LEARN sends (commits) the note.
// ─────────────────────────────────────────────────────────────
static const char _notChars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?-:abcdefghijklmnopqrstuvwxyz";
#define NOT_CHAR_CNT  (sizeof(_notChars) - 1)

static void _notNewNote() {
    static char title[21] = "";
    static char body[81]  = "";
    memset(title, 0, sizeof(title));
    memset(body, 0, sizeof(body));

    int8_t  charIdx = 0;
    bool    editingTitle = true;
    bool    dirty = true;
    inputSettle(150);

    while (true) {
        inputPoll();
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer();
            _notTitle(editingTitle ? "New Note - Title" : "New Note - Body");
            u8g2.setFont(u8g2_font_6x10_tr);

            char preview[22];
            const char* cur = editingTitle ? title : body;
            size_t len = strlen(cur);
            snprintf(preview, 22, "%s%c", cur, _notChars[charIdx]);
            u8g2.drawStr(0, 28, preview);

            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(0, 42, editingTitle ? "Title field" : "Body field");
            char hint[32];
            snprintf(hint, 32, "Rot=chr Sel=add Lrn=%s",
                     editingTitle ? "body" : "save");
            _notHint(hint);
            u8g2.sendBuffer();
        }

        int8_t s = inputConsumeScrollFast(); uiTouchActivity(); if(inputBackHeld()){inputSettle(150);return;}
        if (s) {
            charIdx = (int8_t)((charIdx + s % (int8_t)NOT_CHAR_CNT + NOT_CHAR_CNT) % NOT_CHAR_CNT);
            dirty = true;
        }

        if (inputSelectFired()) {
            char* buf = editingTitle ? title : body;
            size_t maxLen = editingTitle ? 20 : 80;
            size_t len = strlen(buf);
            if (len < maxLen) {
                buf[len]   = _notChars[charIdx];
                buf[len+1] = '\0';
            }
            dirty = true;
        }

        if (inputLearnFired()) {
            if (editingTitle && strlen(title) > 0) {
                editingTitle = false;
                charIdx = 0;
                dirty = true;
            } else if (!editingTitle && strlen(title) > 0) {
                // Save
                String payload = String(title) + ":" + String(body);
                String resp = "";
                Bridge.call("notes_new", payload.c_str()).result(resp);
                u8g2.clearBuffer();
                _notTitle("Notes");
                u8g2.setFont(u8g2_font_6x10_tr);
                bool ok = resp.startsWith("ok");
                u8g2.drawStr(0, 32, ok ? "Note saved!" : "Save failed");
                if(ok) uiToast("Saved!", 800);
                u8g2.sendBuffer();
                delay(1200);
                _notLoadList();
                inputSettle(150);
                return;
            }
        }

        if (inputBackFired()) {
            // Delete last char
            char* buf = editingTitle ? title : body;
            size_t len = strlen(buf);
            if (len > 0) {
                buf[len - 1] = '\0';
                dirty = true;
            } else if (!editingTitle) {
                editingTitle = true;
                dirty = true;
            } else {
                inputSettle(150);
                return;
            }
        }
        delay(5);
    }
}

// ─────────────────────────────────────────────────────────────
//  Search — type query, trigger notes_search
// ─────────────────────────────────────────────────────────────
static void _notSearch() {
    static char query[21] = "";
    memset(query, 0, sizeof(query));
    int8_t charIdx = 0;
    bool dirty = true;
    inputSettle(150);

    while (true) {
        inputPoll();
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer();
            _notTitle("Search Notes");
            u8g2.setFont(u8g2_font_6x10_tr);
            char preview[22];
            snprintf(preview, 22, "%s%c", query, _notChars[charIdx]);
            u8g2.drawStr(0, 30, preview);
            _notHint("Rot=chr Sel=add Lrn=find");
            u8g2.sendBuffer();
        }
        int8_t s = inputConsumeScrollFast(); uiTouchActivity(); if(inputBackHeld()){inputSettle(150);return;}
        if (s) { charIdx = (int8_t)((charIdx + s + NOT_CHAR_CNT) % NOT_CHAR_CNT); dirty = true; }
        if (inputSelectFired()) {
            size_t len = strlen(query);
            if (len < 20) { query[len] = _notChars[charIdx]; query[len+1] = '\0'; dirty = true; }
        }
        if (inputLearnFired()) {
            // Run search and show list
            _notLoadList(query);
            inputSettle(150);
            return;  // caller shows list
        }
        if (inputBackFired()) {
            size_t len = strlen(query);
            if (len > 0) { query[len - 1] = '\0'; dirty = true; }
            else { inputSettle(150); return; }
        }
        delay(5);
    }
}

// ─────────────────────────────────────────────────────────────
//  Notes main menu (sub-states)
// ─────────────────────────────────────────────────────────────
#define NOT_MENU_CNT 3
static const char* _notMenuItems[NOT_MENU_CNT] = { "Browse", "New Note", "Search" };
static int8_t _notMenuCur = 0;

static void notesRun(uint8_t startIdx = 0) {
    _notMenuCur = (int8_t)startIdx;
    bool dirty = true;
    inputSettle(200);
    _notLoadList();

    // If entering "Browse" or "Search" directly, jump straight in
    if (startIdx == 0) goto BROWSE;
    if (startIdx == 1) goto NEW;
    if (startIdx == 2) goto SEARCH;
    goto MENU;

    MENU:
    while (true) {
        inputPoll();
        if (dirty) {
            dirty = false;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(0, 10, "Notes");
            u8g2.drawHLine(0, 12, 128);
            for (int i = 0; i < NOT_MENU_CNT; i++) {
                int y = 28 + i * 14;
                bool sel = (i == _notMenuCur);
                if (sel) {
                    u8g2.drawRBox(0, y - 10, 124, 12, 2);
                    u8g2.setDrawColor(0);
                    u8g2.drawStr(4, y, _notMenuItems[i]);
                    u8g2.setDrawColor(1);
                } else {
                    u8g2.drawStr(4, y, _notMenuItems[i]);
                }
            }
            u8g2.sendBuffer();
        }
        int8_t s = inputConsumeScrollFast(); uiTouchActivity(); if(inputBackHeld()){inputSettle(150);return;}
        if (s) { _notMenuCur = (int8_t)constrain((int)_notMenuCur + s, 0, NOT_MENU_CNT - 1); dirty = true; }
        if (inputSelectOrLearnFired()) {
            inputSettle(150);
            if (_notMenuCur == 0) goto BROWSE;
            if (_notMenuCur == 1) goto NEW;
            if (_notMenuCur == 2) goto SEARCH;
        }
        if (inputBackFired()) { inputSettle(150); return; }
        delay(5);
    }

    BROWSE: {
        _notLoadList();
        bool bd = true;
        while (true) {
            inputPoll();
            if (bd) { bd = false; _notDrawList("Notes"); }
            int8_t s = inputConsumeScrollFast(); uiTouchActivity(); if(inputBackHeld()){inputSettle(150);return;}
            if (s && _notesCnt) {
                _notesSel = (int8_t)constrain((int)_notesSel + s, 0, (int)_notesCnt - 1);
                bd = true;
            }
            if (inputSelectFired() && _notesCnt) {
                _notViewNote(_notesList[_notesSel].id);
                _notLoadList();
                bd = true;
            }
            if (inputLearnFired() && _notesCnt) {
                // Delete selected
                String resp = "";
                Bridge.call("notes_delete", _notesList[_notesSel].id).result(resp);
                _notLoadList();
                bd = true;
            }
            if (inputBackFired()) { inputSettle(150); goto MENU; }
            delay(5);
        }
    }

    NEW: {
        _notNewNote();
        goto MENU;
    }

    SEARCH: {
        _notSearch();
        // After search, show results in browse-style list
        {
            bool bd = true;
            while (true) {
                inputPoll();
                if (bd) { bd = false; _notDrawList("Search Results"); }
                int8_t s = inputConsumeScrollFast(); uiTouchActivity(); if(inputBackHeld()){inputSettle(150);return;}
                if (s && _notesCnt) { _notesSel = (int8_t)constrain((int)_notesSel + s, 0, (int)_notesCnt - 1); bd = true; }
                if (inputSelectFired() && _notesCnt) { _notViewNote(_notesList[_notesSel].id); bd = true; }
                if (inputBackFired()) { inputSettle(150); goto MENU; }
                delay(5);
            }
        }
    }
}
