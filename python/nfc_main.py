# SPDX-License-Identifier: MPL-2.0
# nfc_main.py — NFC Tool DB RPC handlers for HermesQ
#
# Architecture:
#   MCU (hizmos_nfc.h) ──Bridge.call()──► functions registered here
#
# All NFC database work lives here; the MCU never touches SQLite directly.
# Method names use the "nfc_" prefix so they don't collide with any
# existing RPC methods registered by main.py.
#
# Bridge methods provided:
#   nfc_save          — save a scanned card + block data
#   nfc_list_saved    — list all saved cards (id:name|…)
#   nfc_list_history  — alias for nfc_list_saved (used by emulate history)
#   nfc_get           — get a single card's summary string
#   nfc_get_ndef      — get "TYPE:content" for a card (used by emulate)
#   nfc_get_full      — get "uid|type|ndef_type|ndef_content" (used by fav detail)
#   nfc_get_blocks    — get block data for a card (used by clone from saved)
#   nfc_add_fav       — mark a card as favourite
#   nfc_last_id       — return the id of the most recently inserted card
#   nfc_clear_history — delete all non-favourite cards
#   nfc_clear_db      — delete all cards

import sqlite3
import os
from datetime import datetime
from arduino.app_utils import Bridge

DB_PATH = os.path.expanduser("~/nfc_cards.db")

# ── Schema ─────────────────────────────────────────────────────────────────────
SCHEMA = """
CREATE TABLE IF NOT EXISTS nfc_cards (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT    NOT NULL,
    uid           TEXT    NOT NULL,
    type          TEXT,
    uid_length    INTEGER,
    atqa          TEXT,
    sak           TEXT,
    ndef_type     TEXT,
    ndef_content  TEXT,
    favorite      INTEGER DEFAULT 0,
    created_at    TEXT
);
CREATE TABLE IF NOT EXISTS nfc_blocks (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    card_id      INTEGER NOT NULL,
    block_number INTEGER NOT NULL,
    block_data   TEXT    NOT NULL,
    FOREIGN KEY (card_id) REFERENCES nfc_cards(id) ON DELETE CASCADE
);
"""


def _get_db():
    db = sqlite3.connect(DB_PATH)
    db.execute("PRAGMA journal_mode=WAL;")
    db.execute("PRAGMA foreign_keys=ON;")
    db.executescript(SCHEMA)
    return db


def _now():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


# ── RPC handlers ───────────────────────────────────────────────────────────────

def rpc_save(uid, typ, atqa, sak, ndef_type, ndef_content, name, blocks_csv):
    """
    Save a scanned card.
    blocks_csv format: "4:00112233...|5:AABBCC..."
    Returns "OK:<id>" or "ERR:<reason>"
    """
    try:
        uid_length = len(uid.split(":"))
        db = _get_db()
        cur = db.execute(
            "INSERT INTO nfc_cards "
            "(name,uid,type,uid_length,atqa,sak,ndef_type,ndef_content,created_at)"
            " VALUES (?,?,?,?,?,?,?,?,?)",
            (name, uid, typ, uid_length, atqa, sak, ndef_type, ndef_content, _now())
        )
        card_id = cur.lastrowid
        if blocks_csv:
            for entry in blocks_csv.split("|"):
                if ":" not in entry:
                    continue
                bnum, bdata = entry.split(":", 1)
                db.execute(
                    "INSERT INTO nfc_blocks (card_id,block_number,block_data)"
                    " VALUES (?,?,?)",
                    (card_id, int(bnum), bdata)
                )
        db.commit()
        db.close()
        print(f"[NFC] Saved card id={card_id} uid={uid}")
        return f"OK:{card_id}"
    except Exception as e:
        print(f"[NFC] rpc_save error: {e}")
        return f"ERR:{e}"


def _list_cards(fav_only=False):
    try:
        db = _get_db()
        if fav_only:
            rows = db.execute(
                "SELECT id,name FROM nfc_cards WHERE favorite=1"
                " ORDER BY created_at DESC LIMIT 32"
            ).fetchall()
        else:
            rows = db.execute(
                "SELECT id,name FROM nfc_cards ORDER BY created_at DESC LIMIT 32"
            ).fetchall()
        db.close()
        if not rows:
            return ""
        return "|".join(f"{r[0]}:{r[1]}" for r in rows)
    except Exception as e:
        print(f"[NFC] _list_cards error: {e}")
        return ""


def rpc_list_saved():   return _list_cards(fav_only=False)
def rpc_list_history(): return _list_cards(fav_only=False)


def rpc_get(card_id_str):
    """Returns a human-readable summary string for a card."""
    try:
        db = _get_db()
        row = db.execute(
            "SELECT uid,type,ndef_type,ndef_content FROM nfc_cards WHERE id=?",
            (int(card_id_str),)
        ).fetchone()
        db.close()
        if not row:
            return "Not found"
        return f"UID:{row[0]}\nType:{row[1]}\nNDEF {row[2]}:{row[3]}"
    except Exception as e:
        return f"ERR:{e}"


def rpc_get_ndef(card_id_str):
    """
    Returns "TYPE:content" for use by the emulate engine.
    e.g. "URI:https://example.com" or "TEXT:Hello"
    Returns "" if no NDEF present.
    """
    try:
        db = _get_db()
        row = db.execute(
            "SELECT ndef_type,ndef_content FROM nfc_cards WHERE id=?",
            (int(card_id_str),)
        ).fetchone()
        db.close()
        if not row or not row[0] or row[0] == "NONE":
            return ""
        return f"{row[0]}:{row[1] or ''}"
    except Exception as e:
        return ""


def rpc_get_full(card_id_str):
    """
    Returns "uid|type|ndef_type|ndef_content" — used by fav detail view.
    """
    try:
        db = _get_db()
        row = db.execute(
            "SELECT uid,type,ndef_type,ndef_content FROM nfc_cards WHERE id=?",
            (int(card_id_str),)
        ).fetchone()
        db.close()
        if not row:
            return "Not found"
        return f"{row[0]}|{row[1]}|{row[2]}|{row[3] or ''}"
    except Exception as e:
        return f"ERR:{e}"


def rpc_get_blocks(card_id_str):
    """
    Returns pipe-separated "block_num:hex_data" pairs.
    Used by clone-from-saved and fav-detail clone action.
    """
    try:
        db = _get_db()
        rows = db.execute(
            "SELECT block_number,block_data FROM nfc_blocks"
            " WHERE card_id=? ORDER BY block_number",
            (int(card_id_str),)
        ).fetchall()
        db.close()
        if not rows:
            return ""
        return "|".join(f"{r[0]}:{r[1]}" for r in rows)
    except Exception as e:
        return ""


def rpc_add_fav(card_id_str):
    """Mark a card as favourite. Returns "OK" or "ERR:…"."""
    try:
        db = _get_db()
        db.execute("UPDATE nfc_cards SET favorite=1 WHERE id=?", (int(card_id_str),))
        db.commit()
        db.close()
        return "OK"
    except Exception as e:
        return f"ERR:{e}"


def rpc_last_id():
    """Return the id of the most recently inserted card."""
    try:
        db = _get_db()
        row = db.execute(
            "SELECT id FROM nfc_cards ORDER BY id DESC LIMIT 1"
        ).fetchone()
        db.close()
        return str(row[0]) if row else ""
    except Exception as e:
        return ""


def rpc_clear_history():
    """Delete all non-favourite cards (and their block data via FK cascade)."""
    try:
        db = _get_db()
        db.execute("DELETE FROM nfc_cards WHERE favorite=0")
        db.commit()
        db.close()
        return "OK"
    except Exception as e:
        return f"ERR:{e}"


def rpc_clear_db():
    """Delete ALL cards and block data."""
    try:
        db = _get_db()
        db.execute("DELETE FROM nfc_blocks")
        db.execute("DELETE FROM nfc_cards")
        db.commit()
        db.close()
        return "OK"
    except Exception as e:
        return f"ERR:{e}"


# ── Register all RPC endpoints ─────────────────────────────────────────────────
Bridge.provide("nfc_save",          rpc_save)
Bridge.provide("nfc_list_saved",    rpc_list_saved)
Bridge.provide("nfc_list_history",  rpc_list_history)
Bridge.provide("nfc_get",           rpc_get)
Bridge.provide("nfc_get_ndef",      rpc_get_ndef)
Bridge.provide("nfc_get_full",      rpc_get_full)
Bridge.provide("nfc_get_blocks",    rpc_get_blocks)
Bridge.provide("nfc_add_fav",       rpc_add_fav)
Bridge.provide("nfc_last_id",       rpc_last_id)
Bridge.provide("nfc_clear_history", rpc_clear_history)
Bridge.provide("nfc_clear_db",      rpc_clear_db)

print("[NFC] nfc_main.py ready — NFC RPC handlers registered")
