"""
notes_main.py — Notes backend for HermesQ (Arduino App Lab / UNO Q)

Bridge RPC handlers:
  notes_list   ()             → "id:title|id:title|…"  newest first
  notes_read   (id)           → "title\nline1\nline2…"
  notes_new    (title:body)   → "ok" | "err:…"
  notes_delete (id)           → "ok" | "err:…"
  notes_search (query)        → "id:title|…" filtered by query

FIX (see error: "no such column: id"): create_table() previously didn't
declare an `id` column, and SQLStore doesn't add one implicitly — every
call below that does order_by="id ..." or where="id = ..." then fails.
`id INTEGER PRIMARY KEY AUTOINCREMENT` is now declared explicitly,
matching the pattern already used elsewhere (nfc_main.py, hermesq_db.py).

create_table() only runs CREATE TABLE IF NOT EXISTS, so if the notes
SQLite file was already created by the old, broken code (check the
board's data dir, e.g. /app/data/hermesq_notes.db), this fix alone
won't retroactively add the column to that existing file. Delete that
db file once before the next run so it gets recreated with the correct
schema, e.g.:
    rm /app/data/hermesq_notes.db      # adjust path if different
"""

import datetime
from arduino.app_utils import Bridge
from arduino.app_bricks.dbstorage_sqlstore import SQLStore

_notes_db = SQLStore("hermesq_notes.db")
_notes_db.create_table("notes", {
    "id":         "INTEGER PRIMARY KEY AUTOINCREMENT",
    "created_at": "TEXT",
    "title":      "TEXT",
    "body":       "TEXT",
})
print("[notes_main] Notes DB ready.")


def _format_list(rows) -> str:
    if not rows:
        return ""
    return "|".join(f"{r.get('id','')}:{r.get('title','')[:18]}" for r in rows)


def rpc_notes_list(_arg: str = "") -> str:
    rows = _notes_db.read("notes", order_by="id DESC", limit=20) or []
    return _format_list(rows)


def rpc_notes_read(arg: str) -> str:
    note_id = arg.strip()
    rows = _notes_db.read("notes", where=f"id = {note_id}") or []
    if not rows:
        return "err:not found"
    r = rows[0]
    return f"{r.get('title','')}\n{r.get('body','')}"


def rpc_notes_new(arg: str) -> str:
    parts = arg.split(":", 1)
    if len(parts) != 2 or not parts[0].strip():
        return "err:bad format"
    title, body = parts[0].strip(), parts[1].strip()
    try:
        _notes_db.store("notes", {
            "created_at": datetime.datetime.now().isoformat(timespec="seconds"),
            "title": title,
            "body":  body,
        })
        print(f"[notes_main] Saved: {title!r}")
        return "ok"
    except Exception as e:
        print(f"[notes_main] Error: {e}")
        return f"err:{e}"


def rpc_notes_delete(arg: str) -> str:
    note_id = arg.strip()
    try:
        _notes_db.delete("notes", where=f"id = {note_id}")
        print(f"[notes_main] Deleted id={note_id}")
        return "ok"
    except Exception as e:
        return f"err:{e}"


def rpc_notes_search(arg: str) -> str:
    q = arg.strip().lower()
    rows = _notes_db.read("notes", order_by="id DESC", limit=50) or []
    hits = [r for r in rows if q in r.get("title", "").lower() or q in r.get("body", "").lower()]
    return _format_list(hits[:20])


Bridge.provide("notes_list",   rpc_notes_list)
Bridge.provide("notes_read",   rpc_notes_read)
Bridge.provide("notes_new",    rpc_notes_new)
Bridge.provide("notes_delete", rpc_notes_delete)
Bridge.provide("notes_search", rpc_notes_search)
print("[notes_main] Notes RPC handlers registered.")
