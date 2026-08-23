"""HermesQ — SQLStore helpers (Database brick)."""

from datetime import datetime, timezone

from arduino.app_bricks.dbstorage_sqlstore import SQLStore


def init_db(db: SQLStore) -> None:
    db.create_table("action_log", {
        "id": "INTEGER PRIMARY KEY AUTOINCREMENT",
        "ts": "TEXT",
        "mode": "TEXT",
        "command": "TEXT",
        "intent": "TEXT",
        "result": "TEXT",
        "ok": "INTEGER",
    })
    db.create_table("notes", {
        "id": "INTEGER PRIMARY KEY AUTOINCREMENT",
        "title": "TEXT",
        "body": "TEXT",
        "ts": "TEXT",
    })


def log_action(db: SQLStore, mode: str, command: str, intent: str, result: str) -> None:
    ok = 1 if result.startswith("ok:") else 0
    payload = result[3:] if result.startswith("ok:") else (
        result[4:] if result.startswith("err:") else result
    )
    db.store("action_log", {
        "ts": datetime.now(timezone.utc).isoformat(),
        "mode": mode,
        "command": command,
        "intent": intent,
        "result": payload,
        "ok": ok,
    }, create_table=False)


def get_action_history(db: SQLStore, limit: int = 20) -> list:
    return db.read("action_log", order_by="id DESC", limit=limit)


def get_last_action(db: SQLStore) -> str:
    rows = db.read("action_log", order_by="id DESC", limit=1)
    if not rows:
        return "ok:(none)"
    r = rows[0]
    prefix = "ok" if r.get("ok") else "err"
    return f"{prefix}:{r.get('command', '')} → {r.get('result', '')}"
